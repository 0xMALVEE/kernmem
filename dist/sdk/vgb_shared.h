/*
 * vgb_shared.h - IOCTL contract between the kernel driver and user-mode clients.
 *
 * Compiled in both worlds: _KERNEL_MODE is defined for the driver build.
 * This file is kept byte-identical to sdk/vgb_shared.h; the root build asserts
 * it, because a diverged contract is the classic source of version skew.
 */
#pragma once

#ifdef _KERNEL_MODE
#include <ntdef.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define VGB_DEVICE_NAME      L"\\Device\\VanguardBypass"
#define VGB_SYMLINK_NAME     L"\\DosDevices\\VanguardBypass"
#define VGB_WIN32_NAME       L"\\\\.\\VanguardBypass"

#define VGB_DEVICE_TYPE      0x8000

#define VGB_PROTOCOL_VERSION 3

#define VGB_INIT_MAGIC       0x42475631

#define VGB_MAX_NAME_CHARS   96
#define VGB_MAX_MODULES      512
#define VGB_MAX_XFER         0x10000

#define VGB_IOCTL(fn) CTL_CODE(VGB_DEVICE_TYPE, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VGB_GET_VERSION      VGB_IOCTL(0x800)
#define IOCTL_VGB_SET_CLIENT_PID   VGB_IOCTL(0x801)
#define IOCTL_VGB_GET_PID_BY_NAME  VGB_IOCTL(0x802)
#define IOCTL_VGB_GET_MODULE_BASE  VGB_IOCTL(0x803)
#define IOCTL_VGB_READ             VGB_IOCTL(0x804)
#define IOCTL_VGB_WRITE            VGB_IOCTL(0x805)
#define IOCTL_VGB_ENUM_MODULES     VGB_IOCTL(0x806)
#define IOCTL_VGB_HIDE_MODULE      VGB_IOCTL(0x807)
#define IOCTL_VGB_UNHIDE_MODULE    VGB_IOCTL(0x808)
#define IOCTL_VGB_GET_MAIN_BASE    VGB_IOCTL(0x809)

typedef struct _VGB_INIT_PARAMS
{
    unsigned long Magic;
    unsigned long ClientPid;
    unsigned __int64 ImageBase;
    unsigned __int64 ImageSize;
} VGB_INIT_PARAMS;

typedef struct _VGB_MEM_REQ
{
    unsigned long Pid;
    unsigned long Size;
    unsigned __int64 Address;
} VGB_MEM_REQ;

typedef struct _VGB_NAME_REQ
{
    unsigned long Pid;
    unsigned long Pad;
    wchar_t Name[VGB_MAX_NAME_CHARS];
} VGB_NAME_REQ;

typedef struct _VGB_PID_REQ
{
    unsigned long Pid;
    unsigned long Pad;
} VGB_PID_REQ;

typedef struct _VGB_MODULE_INFO
{
    unsigned __int64 Base;
    unsigned long SizeOfImage;
    unsigned long Pad;
    wchar_t Name[VGB_MAX_NAME_CHARS];
} VGB_MODULE_INFO;

typedef struct _VGB_MODULE_LIST
{
    unsigned long Count;
    unsigned long Truncated;
    VGB_MODULE_INFO Entries[1];
} VGB_MODULE_LIST;
