#include "pe.h"

#include <cstdlib>
#include <cstring>

#include "log.h"
#include "nt.h"

namespace pe
{
    bool Load(const std::wstring& path, IMAGE_INFO* info)
    {
        if (!info)
            return false;

        ZeroMemory(info, sizeof(*info));

        std::vector<BYTE> file;
        if (!nt::ReadFileToMemory(path, &file) || file.size() < sizeof(IMAGE_DOS_HEADER))
        {
            vgb_log::Error("cannot read %ls", path.c_str());
            return false;
        }

        auto dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(file.data());
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        {
            vgb_log::Error("not a PE file (bad DOS signature)");
            return false;
        }

        if (dosHeader->e_lfanew <= 0 ||
            (size_t)dosHeader->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > file.size())
        {
            vgb_log::Error("truncated PE headers");
            return false;
        }

        auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS64>(file.data() + dosHeader->e_lfanew);

        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        {
            vgb_log::Error("not a PE file (bad NT signature)");
            return false;
        }

        if (ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        {
            vgb_log::Error("image is not x64");
            return false;
        }

        if (!(ntHeaders->FileHeader.Characteristics & IMAGE_FILE_DLL) &&
            ntHeaders->OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_NATIVE)
        {
            vgb_log::Warn("image subsystem is not NATIVE — continuing anyway");
        }

        const uint32_t lfanew = (uint32_t)dosHeader->e_lfanew;

        info->imageSize = ntHeaders->OptionalHeader.SizeOfImage;
        info->entryPoint = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        info->preferredBase = ntHeaders->OptionalHeader.ImageBase;
        info->sizeOfHeaders = ntHeaders->OptionalHeader.SizeOfHeaders;
        info->sectionCount = ntHeaders->FileHeader.NumberOfSections;
        info->relocationRva = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        info->relocationSize = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        info->importRva = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        info->importSize = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

        if (!info->imageSize || !info->entryPoint)
        {
            vgb_log::Error("image has no size or no entry point");
            return false;
        }

        if (info->imageSize > 0x4000000)
        {
            vgb_log::Error("image size 0x%llX is implausible", (unsigned long long)info->imageSize);
            return false;
        }

        if (!info->relocationRva || !info->relocationSize)
        {
            vgb_log::Error("image has no relocation directory - it cannot be mapped");
            return false;
        }

        if (info->entryPoint >= info->imageSize)
        {
            vgb_log::Error("entry point lies outside the image");
            return false;
        }

        if (!info->sectionCount || info->sectionCount > 96)
        {
            vgb_log::Error("implausible section count %u", info->sectionCount);
            return false;
        }

        /* The whole section table must be inside the file. */
        const size_t tableEnd = (size_t)lfanew + sizeof(IMAGE_NT_HEADERS64) +
                                (size_t)info->sectionCount * sizeof(IMAGE_SECTION_HEADER);
        if (tableEnd > file.size())
        {
            vgb_log::Error("truncated section table");
            return false;
        }

        if (!info->sizeOfHeaders || info->sizeOfHeaders > file.size())
        {
            vgb_log::Error("bad SizeOfHeaders (%llu) for a %zu byte file",
                           (unsigned long long)info->sizeOfHeaders, file.size());
            return false;
        }

        info->rawSize = file.size();
        info->raw = static_cast<uint8_t*>(malloc(file.size()));
        if (!info->raw)
            return false;

        memcpy(info->raw, file.data(), file.size());

        /* The section table must point into the OWNED copy: `file` dies here. */
        info->sections = IMAGE_FIRST_SECTION(
            reinterpret_cast<PIMAGE_NT_HEADERS64>(info->raw + lfanew));

        /* Every section's raw range must be inside the file. */
        for (uint32_t i = 0; i < info->sectionCount; ++i)
        {
            const PIMAGE_SECTION_HEADER section = &info->sections[i];

            if (!section->SizeOfRawData)
                continue;

            if ((uint64_t)section->PointerToRawData + section->SizeOfRawData > info->rawSize)
            {
                vgb_log::Error("section %u raw range lies outside the file", i);
                free(info->raw);
                ZeroMemory(info, sizeof(*info));
                return false;
            }

            if ((uint64_t)section->VirtualAddress + section->SizeOfRawData > info->imageSize)
            {
                vgb_log::Error("section %u overruns the image", i);
                free(info->raw);
                ZeroMemory(info, sizeof(*info));
                return false;
            }
        }

        return true;
    }

    void Free(IMAGE_INFO* info)
    {
        if (!info)
            return;

        free(info->raw);
        ZeroMemory(info, sizeof(*info));
    }

    bool Relocate(uint64_t localImage, uint64_t kernelImage, uint64_t delta)
    {
        auto ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS64>(
            localImage + reinterpret_cast<PIMAGE_DOS_HEADER>(localImage)->e_lfanew);

        auto directory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (!directory.VirtualAddress || !directory.Size)
            return true;

        auto block = reinterpret_cast<PIMAGE_BASE_RELOCATION>(localImage + directory.VirtualAddress);
        uint64_t processed = 0;

        while (processed < directory.Size)
        {
            /* A block must at least cover its own header and stay inside the
             * directory, otherwise the walk can run off the image. */
            if (!block->SizeOfBlock || block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION))
                break;

            if (processed + block->SizeOfBlock > directory.Size)
            {
                vgb_log::Warn("relocation block overruns the directory");
                break;
            }

            const size_t count =
                (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
            auto entries = reinterpret_cast<uint16_t*>(block + 1);

            for (size_t i = 0; i < count; ++i)
            {
                const uint16_t type = entries[i] >> 12;
                const uint16_t offset = entries[i] & 0xFFF;

                if (type == IMAGE_REL_BASED_DIR64)
                {
                    auto address = reinterpret_cast<uint64_t*>(
                        localImage + block->VirtualAddress + offset);
                    *address += delta;
                }
                else if (type == IMAGE_REL_BASED_HIGHLOW)
                {
                    /* 32-bit fixups do not appear in x64 images, but a
                     * non-zero delta would silently corrupt them. */
                    auto address = reinterpret_cast<uint32_t*>(
                        localImage + block->VirtualAddress + offset);
                    *address += (uint32_t)delta;
                }
                else if (type != IMAGE_REL_BASED_ABSOLUTE)
                {
                    vgb_log::Warn("unsupported relocation type %u", type);
                }
            }

            processed += block->SizeOfBlock;
            block = reinterpret_cast<PIMAGE_BASE_RELOCATION>(
                reinterpret_cast<uint8_t*>(block) + block->SizeOfBlock);
        }

        UNREFERENCED_PARAMETER(kernelImage);
        return true;
    }

    bool ResolveImports(uint64_t localImage, uint64_t kernelImage, uint64_t importRva,
                        uint64_t (*resolveExport)(const std::string&, const std::string&))
    {
        if (!importRva || !resolveExport)
            return true;

        auto descriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(localImage + importRva);
        bool ok = true;

        for (; descriptor->Name; ++descriptor)
        {
            const char* moduleName =
                reinterpret_cast<const char*>(localImage + descriptor->Name);

            /* Names come from OriginalFirstThunk, resolved addresses go to
             * FirstThunk. Indexing both explicitly avoids pointer arithmetic
             * across two different arrays. */
            const uint64_t lookupRva = descriptor->OriginalFirstThunk
                                           ? descriptor->OriginalFirstThunk
                                           : descriptor->FirstThunk;

            auto names = reinterpret_cast<PIMAGE_THUNK_DATA64>(localImage + lookupRva);
            auto slots = reinterpret_cast<uint64_t*>(localImage + descriptor->FirstThunk);

            if (!names || !slots)
                continue;

            for (size_t index = 0; names[index].u1.AddressOfData; ++index)
            {
                const bool byOrdinal =
                    (names[index].u1.Ordinal & IMAGE_ORDINAL_FLAG64) != 0;
                std::string function;

                if (!byOrdinal)
                {
                    auto import = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                        localImage + names[index].u1.AddressOfData);
                    function = reinterpret_cast<const char*>(import->Name);
                }
                else
                {
                    function = "#" + std::to_string(names[index].u1.Ordinal & 0xFFFF);
                }

                const uint64_t resolved = resolveExport(moduleName, function);
                if (!resolved)
                {
                    vgb_log::Error("unresolved import %s!%s", moduleName, function.c_str());
                    ok = false;
                    continue;
                }

                slots[index] = resolved;
            }
        }

        UNREFERENCED_PARAMETER(kernelImage);
        return ok;
    }
}
