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

            {
                static int reports = 0;
                if (reports < 40)
                {
                    ++reports;
                    std::printf("[state %d] name-len=%zu name=[%ls]\n", reports,
                                name.size(), name.c_str());
                }
            }

            std::vector<std::wstring> candidates;

            if (!name.empty())
            {
                /*
                 * GLOBALROOT first. The \DosDevices alias is not reliably there -
                 * a name inherited from a dead image resolves through neither, and
                 * the alias is the one that goes missing - while the device object
                 * itself is always reachable by its full path.
                 */
                candidates.push_back(L"\\\\?\\GLOBALROOT\\Device\\" + name);
                candidates.push_back(L"\\\\\\.\\" + name);
            }

            candidates.push_back(L"\\\\?\\GLOBALROOT\\Device\\Kernmem");
            candidates.push_back(KM_WIN32_NAME);

            for (const std::wstring& path : candidates)
            {
                device_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

                if (device_ != INVALID_HANDLE_VALUE)
                {
                    /* Registers this process's block. A failure is not fatal - the
                     * reads below simply stay on the IOCTL path. */
                    RegisterShared();
                    return true;
                }

                /* First few attempts only, so a long wait does not bury the log. */
                static int shown = 0;
                if (shown < 12)
                {
                    ++shown;
                    std::printf("[open] len=%zu path=[%ls] -> %lu\n", path.size(),
                                path.c_str(), GetLastError());
                }
            }

            return false;
        }

        void Close()
        {
            /* Deregistered first: the driver has to drop its mapping while it still
             * knows the process, or the pages go away under a live worker. */
            ReleaseShared();

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

            /* One descriptor, same block, same reasoning as ReadBatch. Only taken
             * when the whole range fits in a single shared transfer. */
            if (size && size <= KM_SHARED_DATA)
            {
                KM_BATCH_DESC entry = { address, (uint32_t)size, 0 };

                if (SharedRead(pid, &entry, 1, buffer, (uint32_t)size))
                    return true;
            }

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

            /* Shared block first: no IOCTL, so no syscall patch is live. Falls
             * through to the IOCTL path when the block is not up. */
            if (SharedRead(pid, entries, count, out, outSize))
                return true;

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
            const errno_t opened = _wfopen_s(&file, path.c_str(), L"rb");

            {
                static int reports = 0;
                if (reports < 80)
                {
                    const DWORD attributes = GetFileAttributesW(path.c_str());

                    ++reports;
                    std::printf("[read %d] exists=%d attr=%lu fopen=%d\n", reports,
                                (int)attributes != -1, attributes, (int)opened);
                }
            }

            if (opened != 0 || !file)
                return std::wstring();

            char line[KM_MAX_DEVICE_CHARS * 2] = { 0 };
            std::wstring device;

            for (int index = 0; index < 3; ++index)
            {
                if (!std::fgets(line, sizeof(line), file))
                    break;

                if (index != 2)
                    continue;

                /*
                 * Strip by byte class, not by a character set string. The set
                 * used to be written "\\r\\n", which in C++ is backslash, 'r' and
                 * 'n' as literals - a real CR/LF is in none of those, so the line
                 * ending was kept and every path was built as "...name\n". That
                 * fails with 2, which reads exactly like a missing device.
                 */
                std::string text(line);

                while (!text.empty() && static_cast<unsigned char>(text.back()) <= ' ')
                    text.pop_back();

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

        /*
         * Shared block. Registered once per open; while it is up the driver's worker
         * services it, so a read is a memcpy and a short spin instead of an IOCTL.
         * That matters because every IOCTL on this path patches and restores a
         * syscall - the block removes the patch from the hot path entirely.
         */
        bool RegisterShared()
        {
            if (shared_)
                return true;

            sharedFailures_ = 0;

            void* block = VirtualAlloc(nullptr, sizeof(KM_SHARED), MEM_COMMIT | MEM_RESERVE,
                                       PAGE_READWRITE);

            if (!block)
                return false;

            ZeroMemory(block, sizeof(KM_SHARED));
            shared_ = static_cast<KM_SHARED*>(block);

            KM_BLOCK_REQ request = { 0 };
            request.User = reinterpret_cast<uint64_t>(shared_);
            request.Size = (uint32_t)sizeof(KM_SHARED);

            if (!Call(IOCTL_KM_SET_CLIENT_BLOCK, &request, sizeof(request), nullptr, 0))
            {
                VirtualFree(shared_, 0, MEM_RELEASE);
                shared_ = nullptr;
                return false;
            }

            return true;
        }

        void ReleaseShared()
        {
            if (!shared_)
                return;

            /* Zero user is the deregistration request; the driver unmaps and drops
             * the worker's reference before the pages go away. */
            KM_BLOCK_REQ request = { 0 };
            Call(IOCTL_KM_SET_CLIENT_BLOCK, &request, sizeof(request), nullptr, 0);

            VirtualFree(shared_, 0, MEM_RELEASE);
            shared_ = nullptr;
        }

        /*
         * One slot per descriptor, so a batch runs concurrently inside the worker.
         * Seq is written last by us and Done last by the driver, so a slot is only
         * touched when the two differ. Returns false whenever the caller should fall
         * back to the IOCTL path.
         */
        bool SharedRead(uint32_t pid, const KM_BATCH_DESC* entries, uint32_t count,
                        void* out, uint32_t outSize)
        {
            uint32_t seq[KM_SHARED_SLOTS] = { 0 };
            uint32_t slot;

            /*
             * After a few failures the block is treated as gone for the rest of
             * the session. Without that, a block that stopped being serviced made
             * every frame pay the full wait before falling back to IOCTL.
             */
            if (!shared_ || sharedFailures_ >= 3 || shared_->Ready != 1 ||
                shared_->Magic != KM_SHARED_MAGIC)
                return false;

            if (!count || count > KM_SHARED_SLOTS || !outSize || outSize > KM_SHARED_DATA)
                return false;

            for (slot = 0; slot < count; ++slot)
            {
                KM_SHARED_REQ* request = &shared_->Reqs[slot];
                const uint32_t size = entries[slot].Size;

                if (!size || entries[slot].Offset >= outSize ||
                    size > outSize - entries[slot].Offset)
                    return false;

                request->Pid = pid;
                request->Address = entries[slot].Address;
                request->Size = size;
                request->Offset = entries[slot].Offset;
                request->Status = 1;

                seq[slot] = request->Seq + 1;
                request->Seq = seq[slot];
            }

            for (slot = 0; slot < count; ++slot)
            {
                KM_SHARED_REQ* request = &shared_->Reqs[slot];
                int spin = 0;

                /*
                 * Short spin first: a serviced slot answers in well under that, and
                 * spinning is the cheapest way to take it. Past it the CPU is handed
                 * over instead - the worker runs at PASSIVE_LEVEL on another thread,
                 * so a pure spin here starves it of exactly the time it needs and
                 * starves the overlay pump on this thread too. That was the hang.
                 */
                while (request->Done != seq[slot] && spin < 20000)
                {
                    YieldProcessor();
                    ++spin;
                }

                for (spin = 0; request->Done != seq[slot] && spin < 200; ++spin)
                    SwitchToThread();

                if (request->Done != seq[slot])
                {
                    ++sharedFailures_;
                    return false;
                }
            }

            sharedFailures_ = 0;
            std::memcpy(out, shared_->Data, outSize);
            return true;
        }

        HANDLE   device_ = INVALID_HANDLE_VALUE;
        uint32_t deviceType_ = KM_DEVICE_TYPE;
        NTSTATUS lastStatus_ = STATUS_SUCCESS;
        KM_SHARED* shared_ = nullptr;
        int sharedFailures_ = 0;   /* consecutive shared-read misses; 3 disables it */
    };
}
