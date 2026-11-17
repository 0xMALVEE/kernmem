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

    /* Stops and removes every loaded service whose display name contains "iqvw".
     * Used when the random name was lost with the state file, which otherwise left
     * \Device\Nal held until a reboot. */
    int StopAllIqvw();
}