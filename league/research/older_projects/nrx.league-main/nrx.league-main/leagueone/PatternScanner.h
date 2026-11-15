#pragma once
#include <vector>

namespace PatternScanner {
    // Finds the first occurrence of a byte pattern in a memory region.
    uintptr_t FindPattern(uintptr_t start, uintptr_t size, const std::vector<int>& pattern);
}