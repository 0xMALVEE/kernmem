#include "PatternScanner.h"
#include <Windows.h>

namespace PatternScanner {
    uintptr_t FindPattern(uintptr_t start, uintptr_t size, const std::vector<int>& pattern) {
        const unsigned char* pData = (const unsigned char*)start;
        const size_t patternSize = pattern.size();

        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t current = start;
        uintptr_t end = start + size;

        while (current < end) {
            // Query the memory region to check protections
            if (!VirtualQuery((LPCVOID)current, &mbi, sizeof(mbi))) break;

            uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (regionEnd > end) regionEnd = end;

            // Check if page is committed and readable (and not a guard page)
            bool isReadable = (mbi.State == MEM_COMMIT) &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
                !(mbi.Protect & PAGE_GUARD);

            if (isReadable) {
                const unsigned char* regionStart = (const unsigned char*)current;
                size_t regionSize = regionEnd - current;

                for (size_t i = 0; i < regionSize - patternSize; ++i) {
                    bool found = true;
                    for (size_t j = 0; j < patternSize; ++j) {
                        if (pattern[j] != -1 && regionStart[i + j] != (unsigned char)pattern[j]) {
                            found = false;
                            break;
                        }
                    }
                    if (found) return (uintptr_t)regionStart + i;
                }
            }
            current = regionEnd;
        }
        return 0;
    }
}