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

        /*
         * A complete key, not just ImagePath and Type. An installer always writes
         * Start and ErrorControl and almost always a DisplayName, so a key carrying
         * two values out of six is itself the anomaly - regardless of how random the
         * name is.
         */
        const DWORD errorControl = 1;
        const DWORD start = 3; /* SERVICE_DEMAND_START */

        status = RegSetKeyValueW(key, nullptr, L"Type", REG_DWORD, &kServiceTypeKernel, sizeof(DWORD));
        if (status == ERROR_SUCCESS)
            status = RegSetKeyValueW(key, nullptr, L"ErrorControl", REG_DWORD, &errorControl, sizeof(DWORD));
        if (status == ERROR_SUCCESS)
            status = RegSetKeyValueW(key, nullptr, L"Start", REG_DWORD, &start, sizeof(DWORD));
        if (status == ERROR_SUCCESS)
            status = RegSetKeyValueW(key, nullptr, L"DisplayName", REG_SZ, serviceName.c_str(),
                                     (DWORD)((serviceName.size() + 1) * sizeof(wchar_t)));

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

    NTSTATUS RemoveKeyForImage(const std::wstring& imagePath, bool stopRunning)
    {
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
        if (!manager)
            return STATUS_REGISTRY_IO_FAILED;

        DWORD needed = 0;
        DWORD count = 0;

        EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER, SERVICE_STATE_ALL,
                              nullptr, 0, &needed, &count, nullptr, nullptr);

        auto buffer = static_cast<LPBYTE>(malloc(needed + 0x400));

        if (!buffer)
        {
            CloseServiceHandle(manager);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        NTSTATUS result = STATUS_SUCCESS;

        if (EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_DRIVER, SERVICE_STATE_ALL,
                                  buffer, needed + 0x400, &needed, &count, nullptr, nullptr))
        {
            auto entries = reinterpret_cast<LPENUM_SERVICE_STATUS_PROCESSW>(buffer);

            for (DWORD i = 0; i < count; ++i)
            {
                SC_HANDLE service = OpenServiceW(manager, entries[i].lpServiceName,
                                                 SERVICE_STOP | DELETE | SERVICE_QUERY_CONFIG);
                if (!service)
                    continue;

                DWORD configSize = 0;
                QueryServiceConfigW(service, nullptr, 0, &configSize);

                auto config = static_cast<LPQUERY_SERVICE_CONFIGW>(malloc(configSize + 0x100));
                bool match = false;

                /* Stored as \??\C:\...\file.sys, so the file name is the tail. */
                if (config && QueryServiceConfigW(service, config, configSize + 0x100, &configSize))
                {
                    const std::wstring binary = config->lpBinaryPathName;

                    if (binary.size() >= imagePath.size() &&
                        _wcsicmp(binary.c_str() + (binary.size() - imagePath.size()),
                                 imagePath.c_str()) == 0)
                        match = true;
                }

                free(config);
                CloseServiceHandle(service);

                if (!match)
                    continue;

                /* A resident host keeps running; only its key goes. Deleting a key
                 * out from under a loaded service is what HideTrail already does, and
                 * the service stays up until it is unloaded. */
                if (stopRunning)
                {
                    if (!NT_SUCCESS(StopAndRemove(entries[i].lpServiceName)))
                        result = STATUS_REGISTRY_IO_FAILED;
                }
                else if (!NT_SUCCESS(RemoveKey(entries[i].lpServiceName)))
                {
                    result = STATUS_REGISTRY_IO_FAILED;
                }
            }
        }

        free(buffer);
        CloseServiceHandle(manager);
        return result;
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
                SC_HANDLE service = OpenServiceW(manager, entries[i].lpServiceName,
                                                 SERVICE_STOP | DELETE | SERVICE_QUERY_CONFIG);

                if (!service)
                    continue;

                /*
                 * Matched on where the image lives, not on its name. Both the
                 * service name and the file name are random now, so a display-name
                 * test only ever caught the historical fixed pair. Anything running
                 * out of %TEMP% is a drop of ours; the iqvw display name stays as a
                 * fallback for a key written by an older build.
                 */
                bool ours = wcsstr(entries[i].lpDisplayName, L"iqvw") != nullptr;

                if (!ours)
                {
                    DWORD configSize = 0;
                    QueryServiceConfigW(service, nullptr, 0, &configSize);

                    auto config = static_cast<LPQUERY_SERVICE_CONFIGW>(malloc(configSize + 0x100));

                    if (config && QueryServiceConfigW(service, config, configSize + 0x100, &configSize))
                        ours = wcsstr(config->lpBinaryPathName, L"\\Temp\\") != nullptr ||
                               wcsstr(config->lpBinaryPathName, L"\\temp\\") != nullptr;

                    free(config);
                }

                if (!ours)
                {
                    CloseServiceHandle(service);
                    continue;
                }

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

    namespace
    {
        struct BOUNDED_STOP
        {
            const std::wstring* Name;
            NTSTATUS            Status;
        };

        DWORD WINAPI BoundedStopThread(LPVOID context)
        {
            auto* stop = static_cast<BOUNDED_STOP*>(context);
            stop->Status = StopAndRemove(*stop->Name);
            return 0;
        }
    }

    NTSTATUS StopAndRemoveBounded(const std::wstring& serviceName, DWORD timeoutMs,
                                  bool* timedOut)
    {
        BOUNDED_STOP stop = { &serviceName, STATUS_UNSUCCESSFUL };

        if (timedOut)
            *timedOut = false;

        const HANDLE thread = CreateThread(nullptr, 0, BoundedStopThread, &stop, 0, nullptr);

        if (!thread)
            return StopAndRemove(serviceName);

        if (WaitForSingleObject(thread, timeoutMs) != WAIT_OBJECT_0)
        {
            /* The thread is abandoned on purpose: it is parked in the kernel and the
             * process is short-lived either way. */
            if (timedOut)
                *timedOut = true;

            return STATUS_IO_TIMEOUT;
        }

        CloseHandle(thread);
        return stop.Status;
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