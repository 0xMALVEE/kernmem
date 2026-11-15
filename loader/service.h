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

    /* Stops and removes every loaded service whose display name contains "iqvw".
     * Used when the random name was lost with the state file, which otherwise left
     * \Device\Nal held until a reboot. */
    int StopAllIqvw();
}