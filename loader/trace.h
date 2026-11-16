/*
 * trace.h - kernel trace scrubbing.
 *
 * Manual mapping never registers the image, so our own driver leaves no trace.
 * The NAL host does: iqvw64e.sys goes through the real object manager every run,
 * and ntoskrnl records it twice - once in MmUnloadedDrivers (the rolling list of
 * the last 50 unloads) and once in the PiDDB cache, which keeps an entry per
 * driver image ever loaded, keyed by TimeDateStamp.
 *
 * Both are scrubbed here while the NAL host is still open and arbitrary kernel
 * read/write is available. Every entry is validated by name before a byte is
 * written, and a failed scrub is a warning rather than a failure: a stale trace
 * is always better than a corrupted ntoskrnl.
 */
#pragma once

#include <cstdint>
#include <string>

namespace trace
{
    /* Clears imageName (a bare file name such as L"iqvw64e.sys") out of
     * MmUnloadedDrivers. Returns how many entries were cleared. */
    int ScrubUnloadedDrivers(const std::wstring& imageName);

    /* Unlinks imageName from the PiDDB cache list. Returns how many entries
     * were unlinked. */
    int ScrubPiddbCache(const std::wstring& imageName);

    /* Both of the above, with logging. Safe to call when nothing matches. */
    void Scrub(const std::wstring& imageName);
}
