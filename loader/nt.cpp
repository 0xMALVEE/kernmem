#include "nt.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace nt
{
    std::wstring GetFullTempPath()
    {
        wchar_t tempDirectory[MAX_PATH + 1] = { 0 };
        const uint32_t length = GetTempPathW(MAX_PATH + 1, tempDirectory);

        if (!length || length > MAX_PATH + 1)
            return L"";

        if (tempDirectory[length - 1] == L'\\')
            tempDirectory[length - 1] = 0;

        return std::wstring(tempDirectory);
    }

    std::wstring GetCurrentAppFolder()
    {
        wchar_t buffer[MAX_PATH + 1] = { 0 };
        GetModuleFileNameW(nullptr, buffer, MAX_PATH + 1);

        std::wstring path(buffer);
        const std::wstring::size_type pos = path.find_last_of(L"\\/");

        return pos == std::wstring::npos ? path : path.substr(0, pos);
    }

    bool ReadFileToMemory(const std::wstring& filePath, std::vector<BYTE>* outBuffer)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        outBuffer->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    bool CreateFileFromMemory(const std::wstring& filePath, const char* address, size_t size)
    {
        std::ofstream file(filePath.c_str(), std::ios_base::out | std::ios_base::binary);
        if (!file.write(address, (std::streamsize)size))
            return false;

        file.close();
        return true;
    }

    uint64_t GetKernelModuleAddress(const std::string& moduleName)
    {
        void* buffer = nullptr;
        DWORD bufferSize = 0;

        NTSTATUS status = NtQuerySystemInformation(
            (SYSTEM_INFORMATION_CLASS)SystemModuleInformation, buffer, bufferSize, &bufferSize);

        while (status == STATUS_INFO_LENGTH_MISMATCH)
        {
            if (buffer)
                VirtualFree(buffer, 0, MEM_RELEASE);

            buffer = VirtualAlloc(nullptr, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!buffer)
                return 0;

            status = NtQuerySystemInformation(
                (SYSTEM_INFORMATION_CLASS)SystemModuleInformation, buffer, bufferSize, &bufferSize);
        }

        if (!NT_SUCCESS(status) || !buffer)
        {
            if (buffer)
                VirtualFree(buffer, 0, MEM_RELEASE);
            return 0;
        }

        const auto modules = static_cast<PRTL_PROCESS_MODULES>(buffer);
        uint64_t result = 0;

        for (ULONG i = 0; i < modules->NumberOfModules; ++i)
        {
            const char* current = reinterpret_cast<char*>(modules->Modules[i].FullPathName) +
                                  modules->Modules[i].OffsetToFileName;

            if (_stricmp(current, moduleName.c_str()) == 0)
            {
                result = reinterpret_cast<uint64_t>(modules->Modules[i].ImageBase);
                break;
            }
        }

        VirtualFree(buffer, 0, MEM_RELEASE);
        return result;
    }

    BOOLEAN bDataCompare(const BYTE* pData, const BYTE* bMask, const char* szMask)
    {
        for (; *szMask; ++szMask, ++pData, ++bMask)
        {
            if (*szMask == 'x' && *pData != *bMask)
                return FALSE;
        }

        return (*szMask) == 0;
    }

    uintptr_t FindPattern(uintptr_t dwAddress, uintptr_t dwLen, BYTE* bMask, const char* szMask)
    {
        const size_t maskLength = strlen(szMask);
        if (dwLen < maskLength)
            return 0;

        const size_t maxLength = dwLen - maskLength;
        for (uintptr_t i = 0; i <= maxLength; ++i)
        {
            if (bDataCompare((BYTE*)(dwAddress + i), bMask, szMask))
                return dwAddress + i;
        }

        return 0;
    }

    PVOID FindSection(const char* sectionName, uintptr_t modulePtr, PULONG size)
    {
        const size_t nameLength = strlen(sectionName);
        PIMAGE_NT_HEADERS headers =
            (PIMAGE_NT_HEADERS)(modulePtr + ((PIMAGE_DOS_HEADER)modulePtr)->e_lfanew);
        PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(headers);

        for (WORD i = 0; i < headers->FileHeader.NumberOfSections; ++i)
        {
            PIMAGE_SECTION_HEADER section = &sections[i];

            if (memcmp(section->Name, sectionName, nameLength) == 0 &&
                nameLength == strlen((char*)section->Name))
            {
                if (!section->VirtualAddress)
                    return nullptr;
                if (size)
                    *size = section->Misc.VirtualSize;
                return (PVOID)(modulePtr + section->VirtualAddress);
            }
        }

        return nullptr;
    }
}