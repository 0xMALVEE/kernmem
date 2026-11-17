/*
 * mapper.h - manual mapper.
 *
 * Maps an unsigned kernel image into kernel pool memory through the NAL
 * primitive, then calls its entry point. Nothing is written to disk and no
 * service key or module list entry is created, so the image is invisible to
 * NtQuerySystemInformation(SystemModuleInformation).
 */
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

/* KM_MAX_DEVICE_CHARS, for the device name the driver reports back. */
#include "km_shared_loader.h"

namespace mapper
{
    typedef struct _MAP_RESULT
    {
        uint64_t imageBase;    /* kernel pool allocation holding the image */
        uint64_t imageSize;
        uint64_t driverObject; /* kernel pool DRIVER_OBJECT for teardown */
        uint64_t entryPoint;   /* resolved kernel VA of DriverEntry */
        NTSTATUS status;       /* status returned by DriverEntry */
        /* Device name the driver actually created. A name still owned by a dead
         * image is suffixed, so this is not always the one that was requested. */
        wchar_t deviceName[KM_MAX_DEVICE_CHARS];
    } MAP_RESULT;

    /*
     * Maps the image at driverPath and calls DriverEntry.
     * clientPid is handed to the driver through its init block so it can
     * re-grant handle access rights to that process.
     */
    bool Map(const std::wstring& driverPath, uint32_t clientPid, MAP_RESULT* result);

    /* Calls the mapped driver's DriverUnload and frees the image. */
    bool Unmap(const MAP_RESULT& result);
}
