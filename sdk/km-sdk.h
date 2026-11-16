/*
 * km-sdk.h - C++ client for the Kernmem driver.
 *
 * Header-only. Link advapi32.lib and ntdll.lib.
 *
 * The SDK does three things the raw IOCTL surface does not:
 *   - version handshake, so a stale loader/driver pair fails loudly
 *   - automatic 64 KiB chunking for reads and writes of any size
 *   - typed accessors and a LastStatus() for real error handling
 */
#pragma once

#include <windows.h>
#include <winternl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <vector>

#include "km_shared.h"

#pragma comment(lib, "ntdll.lib")

#pragma warning(push)
#pragma warning(disable : 4005)
#include <ntstatus.h>
#pragma warning(pop)

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

namespace km
{
    /* Maps a Win32 error to the NTSTATUS the driver actually returned. Declared
     * before the class: member bodies defined in-class cannot see a later
     * namespace-scope declaration. */
    inline NTSTATUS RtlGetLastWin32Error()
    {
        return static_cast<NTSTATUS>(0xC0000000u | (uint32_t)GetLastError());
    }

    class Client
    {
    public:
        Client() = default;
        ~Client() { Close(); }

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        /* Opens the device. False means the driver is not loaded. */
        bool Open()
        {
            Close();

            /*
             * The device name is per install and read back from the loader's
             * state file. The static namespace stays as the last resort, so a
             * missing state file - an older build, or a service load - still
             * connects, and the device type follows the name that was resolved.
             */
            const std::wstring name = StateDeviceName();
            deviceType_ = KmDeviceTypeFor(name.c_str());

            std::vector<std::wstring> candidates;

            if (!name.empty())
            {
                candidates.push_back(L"\\\\\\.\\" + name);
                candidates.push_back(L"\\\\?\\GLOBALROOT\\Device\\" + name);
            }

            candidates.push_back(KM_WIN32_NAME);
            candidates.push_back(L"\\\\?\\GLOBALROOT\\Device\\Kernmem");

            for (const std::wstring& path : candidates)
            {
                device_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

                if (device_ != INVALID_HANDLE_VALUE)
                    return true;
            }

            return false;
        }

        void Close()
        {
            if (device_ != INVALID_HANDLE_VALUE)
                CloseHandle(device_);

            device_ = INVALID_HANDLE_VALUE;
        }

        bool IsOpen() const { return device_ != INVALID_HANDLE_VALUE; }

        NTSTATUS LastStatus() const { return lastStatus_; }

        /* Protocol probe. Call first; mismatch means loader and driver disagree. */
        uint32_t Version()
        {
            uint32_t version = 0;
            Call(IOCTL_KM_GET_VERSION, nullptr, 0, &version, sizeof(version));
            return version;
        }

        bool RequireVersion(uint32_t expected = KM_PROTOCOL_VERSION)
        {
            return Version() == expected;
        }

        bool SetClientPid(uint32_t pid)
        {
            KM_PID_REQ request = { pid, 0 };
            return Call(IOCTL_KM_SET_CLIENT_PID, &request, sizeof(request), nullptr, 0);
        }

        uint32_t PidByName(const std::wstring& imageName)
        {
            KM_NAME_REQ request = { 0 };
            KM_PID_REQ reply = { 0 };

            request.Pid = 0;
            CopyName(request.Name, imageName);

            if (!Call(IOCTL_KM_GET_PID_BY_NAME, &request, sizeof(request), &reply, sizeof(reply)))
                return 0;

            return reply.Pid;
        }

        uint64_t MainBase(uint32_t pid)
        {
            KM_PID_REQ request = { pid, 0 };
            uint64_t base = 0;

            Call(IOCTL_KM_GET_MAIN_BASE, &request, sizeof(request), &base, sizeof(base));
            return base;
        }

        uint64_t ModuleBase(uint32_t pid, const std::wstring& module)
        {
            KM_NAME_REQ request = { 0 };
            uint64_t base = 0;

            request.Pid = pid;
            CopyName(request.Name, module);

            Call(IOCTL_KM_GET_MODULE_BASE, &request, sizeof(request), &base, sizeof(base));
            return base;
        }

        std::vector<KM_MODULE_INFO> Modules(uint32_t pid)
        {
            std::vector<KM_MODULE_INFO> result;
            std::vector<uint8_t> buffer(1 << 20);
            KM_PID_REQ request = { pid, 0 };
            DWORD returned = 0;

            if (!Call(IOCTL_KM_ENUM_MODULES, &request, sizeof(request),
                      buffer.data(), (DWORD)buffer.size(), &returned))
                return result;

            const auto list = reinterpret_cast<const KM_MODULE_LIST*>(buffer.data());
            result.assign(list->Entries, list->Entries + list->Count);

            return result;
        }

        /* Reads any size; chunked internally. */
        bool Read(uint32_t pid, uint64_t address, void* buffer, size_t size)
        {
            auto out = static_cast<uint8_t*>(buffer);
            size_t done = 0;

            while (done < size)
            {
                const size_t remaining = size - done;
                const uint32_t chunk = (uint32_t)(remaining < KM_MAX_XFER ? remaining : KM_MAX_XFER);
                KM_MEM_REQ request = { pid, chunk, address + done };
                DWORD returned = 0;

                if (!Call(IOCTL_KM_READ, &request, sizeof(request),
                          out + done, chunk, &returned))
                    return false;

                if (!returned)
                    break;

                done += returned;
            }

            return done == size;
        }

        template <typename T>
        bool ReadValue(uint32_t pid, uint64_t address, T* out)
        {
            return Read(pid, address, out, sizeof(T));
        }

        /* Several reads in one IOCTL. Each descriptor carries the offset inside
         * `out` its answer belongs at, so a whole scan costs one round trip. A
         * descriptor that fails leaves its slot untouched - check the fields. */
        bool ReadBatch(uint32_t pid, const KM_BATCH_DESC* entries, uint32_t count,
                       void* out, uint32_t outSize)
        {
            if (!count || count > KM_BATCH_MAX)
                return false;

            KM_BATCH_REQ request = { 0 };
            request.Pid = pid;
            request.Count = count;

            for (uint32_t i = 0; i < count; ++i)
                request.Entries[i] = entries[i];

            DWORD returned = 0;

            return Call(IOCTL_KM_READ_BATCH, &request, sizeof(request),
                        out, outSize, &returned);
        }

        bool Write(uint32_t pid, uint64_t address, const void* buffer, size_t size)
        {
            auto in = static_cast<const uint8_t*>(buffer);
            size_t done = 0;

            while (done < size)
            {
                const size_t remaining = size - done;
                const uint32_t chunk = (uint32_t)(remaining < KM_MAX_XFER ? remaining : KM_MAX_XFER);

                std::vector<uint8_t> packet(sizeof(KM_MEM_REQ) + chunk);
                auto request = reinterpret_cast<KM_MEM_REQ*>(packet.data());

                request->Pid = pid;
                request->Size = chunk;
                request->Address = address + done;
                std::memcpy(packet.data() + sizeof(KM_MEM_REQ), in + done, chunk);

                if (!Call(IOCTL_KM_WRITE, packet.data(), (DWORD)packet.size(), nullptr, 0))
                    return false;

                done += chunk;
            }

            return true;
        }

        template <typename T>
        bool WriteValue(uint32_t pid, uint64_t address, const T& value)
        {
            return Write(pid, address, &value, sizeof(T));
        }

        bool HideModule(uint32_t pid, uint64_t moduleBase)
        {
            struct { uint32_t Pid; uint32_t Pad; uint64_t Base; } request = { pid, 0, moduleBase };
            return Call(IOCTL_KM_HIDE_MODULE, &request, sizeof(request), nullptr, 0);
        }

        bool UnhideModule(uint32_t pid, uint64_t moduleBase)
        {
            struct { uint32_t Pid; uint32_t Pad; uint64_t Base; } request = { pid, 0, moduleBase };
            return Call(IOCTL_KM_UNHIDE_MODULE, &request, sizeof(request), nullptr, 0);
        }

    private:
        static void CopyName(wchar_t* target, const std::wstring& source)
        {
            const size_t count = source.size() < KM_MAX_NAME_CHARS - 1
                                     ? source.size()
                                     : KM_MAX_NAME_CHARS - 1;

            std::memcpy(target, source.data(), count * sizeof(wchar_t));
            target[count] = 0;
        }

        /* Third line of the loader's state file, or empty when there is none. */
        static std::wstring StateDeviceName()
        {
            wchar_t temp[MAX_PATH + 1] = { 0 };

            if (!GetTempPathW(MAX_PATH + 1, temp))
                return std::wstring();

            std::wstring path(temp);
            path += L"km-nal.state";

            std::FILE* file = nullptr;
            if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
                return std::wstring();

            char line[KM_MAX_DEVICE_CHARS * 2] = { 0 };
            std::wstring device;

            for (int index = 0; index < 3; ++index)
            {
                if (!std::fgets(line, sizeof(line), file))
                    break;

                if (index != 2)
                    continue;

                std::string text(line);
                const std::string::size_type end = text.find_last_not_of("\\r\\n");
                text.erase(end == std::string::npos ? 0 : end + 1);
                device.assign(text.begin(), text.end());
            }

            std::fclose(file);
            return device;
        }

        bool Call(ULONG code, const void* in, DWORD inSize, void* out, DWORD outSize,
                  DWORD* returned = nullptr)
        {
            if (!IsOpen())
            {
                lastStatus_ = STATUS_INVALID_HANDLE;
                return false;
            }

            /* The device type is derived from the per-install name, so a code
             * built with the compile-time value has to be re-stamped. */
            DWORD bytes = 0;
            const BOOL ok = DeviceIoControl(device_, KmRecodeIoctl(code, deviceType_),
                                            const_cast<void*>(in), inSize,
                                            out, outSize, &bytes, nullptr);

            if (returned)
                *returned = bytes;

            lastStatus_ = ok ? STATUS_SUCCESS : static_cast<NTSTATUS>(RtlGetLastWin32Error());
            return ok != FALSE;
        }

        HANDLE   device_ = INVALID_HANDLE_VALUE;
        uint32_t deviceType_ = KM_DEVICE_TYPE;
        NTSTATUS lastStatus_ = STATUS_SUCCESS;
    };
}
