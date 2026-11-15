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
}