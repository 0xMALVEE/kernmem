#include "service.h"

#include "log.h"
#include "nt.h"

namespace service
{
    static const DWORD kServiceTypeKernel = 1;

    NTSTATUS RegisterAndStart(const std::wstring& driverPath, const std::wstring& serviceName)
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