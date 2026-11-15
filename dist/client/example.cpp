/*
 * example.cpp - end-to-end smoke test for the SDK.
 *
 * Run it while the game is up and the driver is loaded. It proves the whole
 * chain: device open, version handshake, pid resolution, module lookup and a
 * read of the target's PE header.
 *
 * Build: see client/build.ps1
 */
#include <windows.h>

#include <cstdio>

#include "../sdk/vgb-sdk.h"

int main()
{
    vgb::Client client;

    if (!client.Open())
    {
        printf("driver is not loaded - run: vgb-loader load\n");
        return 1;
    }

    const uint32_t version = client.Version();
    if (version != VGB_PROTOCOL_VERSION)
    {
        printf("protocol mismatch: driver %u, sdk %u\n", version, VGB_PROTOCOL_VERSION);
        return 2;
    }

    const uint32_t pid = client.PidByName(L"League of Legends.exe");
    if (!pid)
    {
        printf("game process not found (only present during a match)\n");
        return 3;
    }

    printf("pid: %u\n", pid);

    /* Register ourselves so plain ReadProcessMemory also works. */
    client.SetClientPid(GetCurrentProcessId());

    const uint64_t base = client.MainBase(pid);
    if (!base)
    {
        printf("could not resolve the image base\n");
        return 4;
    }

    printf("image base: 0x%llX\n", (unsigned long long)base);

    uint16_t dosMagic = 0;
    if (!client.ReadValue(pid, base, &dosMagic) || dosMagic != IMAGE_DOS_SIGNATURE)
    {
        printf("PE header read failed (status 0x%08X)\n", (unsigned)client.LastStatus());
        return 5;
    }

    int32_t lfanew = 0;
    client.ReadValue(pid, base + 0x3C, &lfanew);

    uint32_t ntSignature = 0;
    client.ReadValue(pid, base + lfanew, &ntSignature);

    if (ntSignature != IMAGE_NT_SIGNATURE)
    {
        printf("bad NT signature\n");
        return 6;
    }

    printf("PE header valid - memory access confirmed\n");

    /* Confirm the handle-restoration path actually took effect. */
    const HANDLE handle = OpenProcess(PROCESS_VM_READ, FALSE, pid);
    if (handle)
    {
        uint16_t value = 0;
        SIZE_T read = 0;
        const BOOL ok = ReadProcessMemory(handle, (LPCVOID)base, &value, sizeof(value), &read);
        CloseHandle(handle);

        printf("ReadProcessMemory: %s\n", ok ? "works" : "still blocked (Path B off)");
    }

    const auto modules = client.Modules(pid);
    printf("modules enumerated: %zu\n", modules.size());

    for (size_t i = 0; i < modules.size() && i < 5; ++i)
        printf("  0x%016llX  %ls\n", (unsigned long long)modules[i].Base, modules[i].Name);

    return 0;
}
