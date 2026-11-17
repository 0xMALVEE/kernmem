/*
 * km_shared.h - IOCTL contract between the kernel driver and user-mode clients.
 *
 * Compiled in both worlds: _KERNEL_MODE is defined for the driver build.
 * This file is kept byte-identical to sdk/km_shared.h; the root build asserts
 * it, because a diverged contract is the classic source of version skew.
 */
#pragma once

#ifdef _KERNEL_MODE
#include <ntdef.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define KM_DEVICE_NAME      L"\\Device\\Kernmem"
#define KM_SYMLINK_NAME     L"\\DosDevices\\Kernmem"
#define KM_WIN32_NAME       L"\\\\.\\Kernmem"

#define KM_DEVICE_TYPE      0x8000

#define KM_PROTOCOL_VERSION 6

#define KM_INIT_MAGIC       0x42475631

#define KM_MAX_DEVICE_CHARS 64
#define KM_MAX_NAME_CHARS   96
#define KM_MAX_MODULES      512
#define KM_MAX_XFER         0x10000

/* The device type is derived from the random device name so a build does not
 * stamp one fixed constant into every IOCTL code. An empty name keeps the
 * historical value, which is also what an old loader gets. */
static __inline unsigned long KmDeviceTypeFor(const wchar_t* name)
{
    unsigned long hash = 0x811C9DC5u;
    unsigned long i;

    if (!name || !name[0])
        return KM_DEVICE_TYPE;

    for (i = 0; name[i] && i < KM_MAX_DEVICE_CHARS; ++i)
        hash = (hash ^ (unsigned long)(unsigned short)name[i]) * 16777619u;

    return (hash & 0x7FFFu) | 0x8000u;
}

/* Re-stamp a code built with the compile-time KM_DEVICE_TYPE onto the device
 * type this install actually uses. Only the low 16 bits (function, method,
 * access) survive; bits 16-31 carry the device type. */
static __inline unsigned long KmRecodeIoctl(unsigned long code, unsigned long deviceType)
{
    return (code & 0x0000FFFFul) | ((deviceType & 0xFFFFul) << 16);
}

#define KM_IOCTL(fn) CTL_CODE(KM_DEVICE_TYPE, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_KM_GET_VERSION      KM_IOCTL(0x800)
#define IOCTL_KM_SET_CLIENT_PID   KM_IOCTL(0x801)
#define IOCTL_KM_GET_PID_BY_NAME  KM_IOCTL(0x802)
#define IOCTL_KM_GET_MODULE_BASE  KM_IOCTL(0x803)
#define IOCTL_KM_READ             KM_IOCTL(0x804)
#define IOCTL_KM_WRITE            KM_IOCTL(0x805)
#define IOCTL_KM_ENUM_MODULES     KM_IOCTL(0x806)
#define IOCTL_KM_HIDE_MODULE      KM_IOCTL(0x807)
#define IOCTL_KM_UNHIDE_MODULE    KM_IOCTL(0x808)
#define IOCTL_KM_GET_MAIN_BASE    KM_IOCTL(0x809)
#define IOCTL_KM_GET_INFO         KM_IOCTL(0x80A)
#define IOCTL_KM_READ_BATCH       KM_IOCTL(0x80B)
#define IOCTL_KM_SET_CLIENT_BLOCK KM_IOCTL(0x80C)

typedef struct _KM_INIT_PARAMS
{
    unsigned long Magic;
    unsigned long ClientPid;
    unsigned __int64 ImageBase;
    unsigned __int64 ImageSize;
    unsigned long AltitudeSeed;
    unsigned long PoolTag;
    wchar_t DeviceName[KM_MAX_DEVICE_CHARS];
    /* Client-allocated shared block: user VA and size. The driver locks it with an
     * MDL and maps it into system space, so reads cost no IOCTL at all. */
    unsigned __int64 SharedUser;
    unsigned long SharedSize;
    /* .pdata location, relative to ImageBase. A mapped image is invisible to the SEH
     * dispatcher, so the driver registers its own exception table before any of its
     * code can take a fault - otherwise every __try/__except escalates to 0x1E. */
    unsigned long ExceptionRva;
    unsigned long ExceptionSize;
} KM_INIT_PARAMS;

/* What the driver reports about its own mapping, so teardown needs no state
 * file on disk. Without this a lost file meant the image was never freed. */
typedef struct _KM_INFO
{
    unsigned __int64 ImageBase;
    unsigned __int64 ImageSize;
    unsigned __int64 DriverObject;
} KM_INFO;

typedef struct _KM_MEM_REQ
{
    unsigned long Pid;
    unsigned long Size;
    unsigned __int64 Address;
} KM_MEM_REQ;

/*
 * Batched read. One IOCTL carries up to KM_BATCH_MAX independent
 * (address, size, offset) descriptors and the answer is written back into the
 * output buffer at each offset. A per-frame scan otherwise costs one round trip
 * per field, which is both slow and a very recognisable IOCTL rate.
 */
#define KM_BATCH_MAX 24

typedef struct _KM_BATCH_DESC
{
    unsigned __int64 Address;
    unsigned long Size;
    unsigned long Offset;
} KM_BATCH_DESC;

typedef struct _KM_BATCH_REQ
{
    unsigned long Pid;
    unsigned long Count;
    KM_BATCH_DESC Entries[KM_BATCH_MAX];
} KM_BATCH_REQ;

/* A client registers its OWN block with this, once, right after Open(). From then
 * on its reads are serviced by the driver's worker with no IOCTL, so the hot path
 * never has a syscall patched. The loader keeps a separate block for kernel reads. */
typedef struct _KM_BLOCK_REQ
{
    unsigned __int64 User;
    unsigned long Size;
    unsigned long Pad;
} KM_BLOCK_REQ;

/*
 * Shared section. This is the read path during play: the client writes a
 * descriptor, spins on Done, and copies the answer out of Data. No IOCTL, and so
 * no syscall patch is live while the game runs.
 *
 * Seq is bumped last by the client and Done is set last by the driver, so a slot
 * is only touched when the two differ. Slots are independent, so several reads
 * can be outstanding at once.
 */
#define KM_SHARED_MAGIC 0x4B4D5348u   /* 'KMSH' */
#define KM_SHARED_SLOTS 32
#define KM_SHARED_DATA  0x20000

typedef struct _KM_SHARED_REQ
{
    volatile unsigned long Seq;
    volatile unsigned long Done;
    unsigned long Pid;
    unsigned long Size;
    unsigned __int64 Address;
    unsigned long Offset;
    unsigned long Status;
} KM_SHARED_REQ;

typedef struct _KM_SHARED
{
    unsigned long Magic;
    unsigned long Slots;
    unsigned long DataSize;
    volatile unsigned long Ready;
    volatile unsigned long Heartbeat;
    KM_SHARED_REQ Reqs[KM_SHARED_SLOTS];
    unsigned char Data[KM_SHARED_DATA];
} KM_SHARED;

typedef struct _KM_NAME_REQ
{
    unsigned long Pid;
    unsigned long Pad;
    wchar_t Name[KM_MAX_NAME_CHARS];
} KM_NAME_REQ;

typedef struct _KM_PID_REQ
{
    unsigned long Pid;
    unsigned long Pad;
} KM_PID_REQ;

typedef struct _KM_MODULE_INFO
{
    unsigned __int64 Base;
    unsigned long SizeOfImage;
    unsigned long Pad;
    wchar_t Name[KM_MAX_NAME_CHARS];
} KM_MODULE_INFO;

typedef struct _KM_MODULE_LIST
{
    unsigned long Count;
    unsigned long Truncated;
    KM_MODULE_INFO Entries[1];
} KM_MODULE_LIST;
