#include "trace.h"

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
    int ScrubUnloadedDrivers(const std::wstring& imageName)
    {
        const std::wstring wanted = BaseName(imageName);
        const uint64_t kernelBase = nal::ResolveKernelBase();

        if (wanted.empty() || !kernelBase)
            return 0;

        /* MmUnloadedDrivers is a pointer variable, so the export address has to
         * be dereferenced to reach the array itself. */
        const uint64_t symbol = nal::GetKernelModuleExport(kernelBase, "MmUnloadedDrivers");

        if (!symbol || !KernelRange(symbol, sizeof(uint64_t)))
        {
            km_log::Warn("MmUnloadedDrivers not resolved - unload trace left in place");
            return 0;
        }

        uint64_t array = 0;
        if (!nal::ReadMemory(symbol, &array, sizeof(array)) || !KernelRange(array, kUnloadedEntrySize))
        {
            km_log::Warn("MmUnloadedDrivers pointer is implausible - unload trace left in place");
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

        if (!symbol || !KernelRange(symbol, sizeof(uint64_t)))
        {
            km_log::Warn("PiDDBCacheTable not resolved - cache entry left in place");
            return 0;
        }

        uint64_t table = 0;
        if (!nal::ReadMemory(symbol, &table, sizeof(table)) || !KernelRange(table, 0x80))
        {
            km_log::Warn("PiDDBCacheTable pointer is implausible - cache entry left in place");
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
