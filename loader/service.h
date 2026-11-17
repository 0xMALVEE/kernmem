/*
 * service.h - kernel service registration for the vulnerable driver.
 */
#pragma once

#include <windows.h>
#include <string>

namespace service
{
    NTSTATUS RegisterAndStart(const std::wstring& driverPath, const std::wstring& serviceName);
    NTSTATUS StopAndRemove(const std::wstring& serviceName);

    /* Writes the service key without starting anything, and deletes it without
     * unloading. Splitting the two is what lets the key be hidden right after a
     * successful map: NtUnloadDriver needs a key to name the service by, so it is
     * written back from the same values just before teardown. */
    NTSTATUS WriteKey(const std::wstring& driverPath, const std::wstring& serviceName);
    NTSTATUS RemoveKey(const std::wstring& serviceName);

    /* Removes the key of any service whose ImagePath ends in the given file. Used by
     * the sweep: the service name and the file name are independently random, so the
     * key can only be found by the path it stores.
     *
     * stopRunning false leaves a loaded service alone and drops only its key - the
     * case where the host is still resident and the session has just attached to it.
     */
    NTSTATUS RemoveKeyForImage(const std::wstring& imagePath, bool stopRunning);

    /* Stops and removes every service that runs out of %TEMP%, or whose display name
     * still says "iqvw". Used when the random name was lost with the state file,
     * which otherwise left \Device\Nal held until a reboot. */
    int StopAllIqvw();
}