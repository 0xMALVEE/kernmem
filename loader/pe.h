/*
 * pe.h - minimal PE parsing for the image we are going to map by hand.
 */
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace pe
{
    typedef struct _IMAGE_INFO
    {
        uint8_t*       raw;          /* owned copy of the file */
        size_t         rawSize;
        uint64_t       imageSize;    /* SizeOfImage */
        uint64_t       sizeOfHeaders;
        uint64_t       entryPoint;   /* RVA of the entry point */
        uint64_t       preferredBase;
        uint32_t       sectionCount;
        PIMAGE_SECTION_HEADER sections;
        uint64_t       relocationRva;
        uint64_t       relocationSize;
        uint64_t       importRva;
        uint64_t       importSize;
        uint64_t       exceptionRva;   /* .pdata: RUNTIME_FUNCTION array */
        uint64_t       exceptionSize;
    } IMAGE_INFO;

    /* Reads the file and validates it as an x64 native image with relocations. */
    bool Load(const std::wstring& path, IMAGE_INFO* info);
    void Free(IMAGE_INFO* info);

    /* Applies IMAGE_REL_BASED_DIR64 fixups against the actual load delta. */
    bool Relocate(uint64_t localImage, uint64_t kernelImage, uint64_t delta);

    /* Resolves one import thunk array; writes the resolved addresses in place. */
    bool ResolveImports(uint64_t localImage, uint64_t kernelImage, uint64_t importRva,
                        uint64_t (*resolveExport)(const std::string& module, const std::string& function));
}
