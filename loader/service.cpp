#include "service.h"

#include <cstdlib>
#include <cwchar>

#include "log.h"
#include "nt.h"

namespace service
{
    static const DWORD kServiceTypeKernel = 1;

    NTSTATUS WriteKey(const std::wstring& driverPath, const std::wstring& serviceName)
    {
        const std::wstring servicesPath = L"SYSTEM\\CurrentControlSet\\Services\\" + serviceName;
        const std::wstring imagePath = L"\\??\\" + driverPath;

        HKEY key = nullptr;
        LSTATUS status = RegCreateKeyW(HKEY_LOCAL_MACHINE, servicesPath.c_str(), &key);
        if (status != ERROR_SUCCESS)
            return STATUS_REGISTRY_IO_FAILED;

        status = RegSetKeyValueW(key, nullptr, L"ImagePath", REG_EXPAND_SZ, imagePath.c_str(),
                                 (DWORD)(imagePath.size() * sizeof(wchar_t)));
        if (status != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());
            return STATUS_REGISTRY_IO_FAILED;
        }

        status = RegSetKeyValueW(key, nullptr, L"Type", REG_DWORD, &kServiceTypeKernel, sizeof(DWORD));
        if (status != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());
            return STATUS_REGISTRY_IO_FAILED;
        }

        RegCloseKey(key);
        return STATUS_SUCCESS;
    }

    NTSTATUS RemoveKey(const std::wstring& serviceName)
    {
        const std::wstring servicesPath = L"SYSTEM\\CurrentControlSet\\Services\\" + serviceName;
        const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());

        return (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
                   ? STATUS_SUCCESS
                   : STATUS_REGISTRY_IO_FAILED;
    }

    NTSTATUS RegisterAndStart(const std::wstring& driverPath, const std::wstring& serviceName)
    {
        const std::wstring servicesPath = L"SYSTEM\\CurrentControlSet\\Services\\" + serviceName;

        const NTSTATUS written = WriteKey(driverPath, serviceName);
        if (!NT_SUCCESS(written))
            return written;

        const ULONG kSeLoadDriverPrivilege = 10UL;
        BOOLEAN wasEnabled = FALSE;
        NTSTATUS ntStatus = nt::RtlAdjustPrivilege(kSeLoadDriverPrivilege, TRUE, FALSE, &wasEnabled);
        if (!NT_SUCCESS(ntStatus))
        {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());
            return ntStatus;
        }

        const std::wstring registryPath =
            L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\" + serviceName;
        UNICODE_STRING serviceString;
        RtlInitUnicodeString(&serviceString, registryPath.c_str());

        ntStatus = nt::NtLoadDriver(&serviceString);
        if (!NT_SUCCESS(ntStatus))
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());

        return ntStatus;
    }

    int StopAllIqvw()
    {
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);

        if (!manager)
            return 0;

        DWORD needed = 0;
        DWORD count = 0;
        int stopped = 0;

        EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER,
                              SERVICE_STATE_ALL, nullptr, 0, &needed, &count,
                              nullptr, nullptr);

        auto buffer = static_cast<LPBYTE>(malloc(needed + 0x400));

        if (!buffer)
        {
            CloseServiceHandle(manager);
            return 0;
        }

        if (EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER,
                                  SERVICE_STATE_ALL, buffer, needed + 0x400, &needed,
                                  &count, nullptr, nullptr))
        {
            auto entries = reinterpret_cast<LPENUM_SERVICE_STATUS_PROCESSW>(buffer);

            for (DWORD i = 0; i < count; ++i)
            {
                if (!wcsstr(entries[i].lpDisplayName, L"iqvw"))
                    continue;

                SC_HANDLE service = OpenServiceW(manager, entries[i].lpServiceName,
                                                 SERVICE_STOP | DELETE);

                if (!service)
                    continue;

                SERVICE_STATUS status = { 0 };
                ControlService(service, SERVICE_CONTROL_STOP, &status);
                DeleteService(service);
                CloseServiceHandle(service);

                ++stopped;
            }
        }

        free(buffer);
        CloseServiceHandle(manager);

        return stopped;
    }

    NTSTATUS StopAndRemove(const std::wstring& serviceName)
    {
        const std::wstring registryPath =
            L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\" + serviceName;
        const std::wstring servicesPath = L"SYSTEM\\CurrentControlSet\\Services\\" + serviceName;

        HKEY key = nullptr;
        LSTATUS status = RegOpenKeyW(HKEY_LOCAL_MACHINE, servicesPath.c_str(), &key);
        if (status != ERROR_SUCCESS)
            return status == ERROR_FILE_NOT_FOUND ? STATUS_SUCCESS : STATUS_REGISTRY_IO_FAILED;

        RegCloseKey(key);

        UNICODE_STRING serviceString;
        RtlInitUnicodeString(&serviceString, registryPath.c_str());

        NTSTATUS ntStatus = nt::NtUnloadDriver(&serviceString);
        if (!NT_SUCCESS(ntStatus))
        {
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());
            return ntStatus;
        }

        status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, servicesPath.c_str());
        return status == ERROR_SUCCESS ? ntStatus : STATUS_REGISTRY_IO_FAILED;
    }
}