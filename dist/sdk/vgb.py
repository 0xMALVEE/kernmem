"""
Python binding for the VanguardBypass driver.

Mirrors sdk/vgb-sdk.h exactly, including the 64 KiB chunking and the version
handshake. ctypes only, no third-party dependency.

    import vgb

    with vgb.Client() as c:
        c.require_version()
        pid = c.pid_by_name("League of Legends.exe")
        print(hex(c.main_base(pid)))
"""

import ctypes
import ctypes.wintypes as wt

DEVICE_PATH = r"\\.\VanguardBypass"

DEVICE_TYPE = 0x8000
METHOD_BUFFERED = 0
FILE_ANY_ACCESS = 0

MAX_NAME_CHARS = 96
MAX_XFER = 0x10000
PROTOCOL_VERSION = 4

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x1
FILE_SHARE_WRITE = 0x2
OPEN_EXISTING = 3
FILE_ATTRIBUTE_NORMAL = 0x80
INVALID_HANDLE_VALUE = wt.HANDLE(-1).value


# Win32 signatures must be declared explicitly. Without this, ctypes assumes a
# 32-bit int return and truncates the 64-bit device handle on x64, which then
# makes every DeviceIoControl fail with ERROR_INVALID_HANDLE (6).
_kernel32 = ctypes.windll.kernel32

_kernel32.CreateFileW.restype = wt.HANDLE
_kernel32.CreateFileW.argtypes = [
    wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p,
    wt.DWORD, wt.DWORD, wt.HANDLE,
]

_kernel32.CloseHandle.restype = wt.BOOL
_kernel32.CloseHandle.argtypes = [wt.HANDLE]

_kernel32.DeviceIoControl.restype = wt.BOOL
_kernel32.DeviceIoControl.argtypes = [
    wt.HANDLE, wt.DWORD, ctypes.c_void_p, wt.DWORD,
    ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p,
]


def _ctl(fn):
    return (DEVICE_TYPE << 16) | (FILE_ANY_ACCESS << 14) | (fn << 2) | METHOD_BUFFERED


IOCTL_GET_VERSION = _ctl(0x800)
IOCTL_SET_CLIENT_PID = _ctl(0x801)
IOCTL_GET_PID_BY_NAME = _ctl(0x802)
IOCTL_GET_MODULE_BASE = _ctl(0x803)
IOCTL_READ = _ctl(0x804)
IOCTL_WRITE = _ctl(0x805)
IOCTL_ENUM_MODULES = _ctl(0x806)
IOCTL_HIDE_MODULE = _ctl(0x807)
IOCTL_UNHIDE_MODULE = _ctl(0x808)
IOCTL_GET_MAIN_BASE = _ctl(0x809)


class MemReq(ctypes.Structure):
    _fields_ = [("pid", wt.ULONG),
                ("size", wt.ULONG),
                ("address", ctypes.c_uint64)]


class PidReq(ctypes.Structure):
    _fields_ = [("pid", wt.ULONG), ("pad", wt.ULONG)]


class NameReq(ctypes.Structure):
    _fields_ = [("pid", wt.ULONG), ("pad", wt.ULONG),
                ("name", wt.WCHAR * MAX_NAME_CHARS)]


class BaseReq(ctypes.Structure):
    _fields_ = [("pid", wt.ULONG), ("pad", wt.ULONG),
                ("base", ctypes.c_uint64)]


class ModuleInfo(ctypes.Structure):
    _fields_ = [("base", ctypes.c_uint64),
                ("size", wt.ULONG),
                ("pad", wt.ULONG),
                ("name", wt.WCHAR * MAX_NAME_CHARS)]


class ModuleList(ctypes.Structure):
    _fields_ = [("count", wt.ULONG), ("truncated", wt.ULONG),
                ("entries", ModuleInfo * 1)]


class VgbError(OSError):
    """Raised when the driver rejects a call."""


class Client:
    def __init__(self, path=DEVICE_PATH):
        self._path = path
        self._handle = INVALID_HANDLE_VALUE
        self.last_status = 0

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_):
        self.close()

    def open(self):
        self.close()
        self._handle = _kernel32.CreateFileW(
            self._path, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, None,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, None)
        if self._handle == INVALID_HANDLE_VALUE:
            raise VgbError("driver is not loaded (device missing)")

    def close(self):
        if self._handle != INVALID_HANDLE_VALUE:
            _kernel32.CloseHandle(self._handle)
            self._handle = INVALID_HANDLE_VALUE

    def _call(self, code, in_buf, in_size, out_buf, out_size):
        if self._handle == INVALID_HANDLE_VALUE:
            raise VgbError("device is not open")

        returned = wt.DWORD(0)
        ok = _kernel32.DeviceIoControl(
            self._handle, code, in_buf, in_size, out_buf, out_size,
            ctypes.byref(returned), None)

        if not ok:
            self.last_status = _kernel32.GetLastError()
            raise VgbError(self.last_status)

        return returned.value

    def version(self):
        out = wt.ULONG(0)
        self._call(IOCTL_GET_VERSION, None, 0, ctypes.byref(out), ctypes.sizeof(out))
        return out.value

    def require_version(self, expected=PROTOCOL_VERSION):
        found = self.version()
        if found != expected:
            raise VgbError(
                "protocol mismatch: driver %d, sdk %d - rebuild loader and driver together"
                % (found, expected))

    def set_client_pid(self, pid):
        req = PidReq(pid, 0)
        self._call(IOCTL_SET_CLIENT_PID, ctypes.byref(req), ctypes.sizeof(req), None, 0)

    def pid_by_name(self, name):
        req = NameReq(0, 0, name)
        out = PidReq(0, 0)
        self._call(IOCTL_GET_PID_BY_NAME, ctypes.byref(req), ctypes.sizeof(req),
                   ctypes.byref(out), ctypes.sizeof(out))
        return out.pid

    def main_base(self, pid):
        req = PidReq(pid, 0)
        out = ctypes.c_uint64(0)
        self._call(IOCTL_GET_MAIN_BASE, ctypes.byref(req), ctypes.sizeof(req),
                   ctypes.byref(out), ctypes.sizeof(out))
        return out.value

    def module_base(self, pid, module):
        req = NameReq(pid, 0, module)
        out = ctypes.c_uint64(0)
        self._call(IOCTL_GET_MODULE_BASE, ctypes.byref(req), ctypes.sizeof(req),
                   ctypes.byref(out), ctypes.sizeof(out))
        return out.value

    def modules(self, pid):
        capacity = 1 << 20
        buf = ctypes.create_string_buffer(capacity)
        req = PidReq(pid, 0)
        returned = self._call(IOCTL_ENUM_MODULES, ctypes.byref(req), ctypes.sizeof(req),
                              buf, capacity)

        count = wt.ULONG.from_buffer(buf).value
        stride = ctypes.sizeof(ModuleInfo)
        header = ctypes.sizeof(ModuleList) - stride

        result = []
        for i in range(count):
            offset = header + i * stride
            if offset + stride > returned:
                break
            result.append(ModuleInfo.from_buffer_copy(buf[offset:offset + stride]))

        return result

    def read(self, pid, address, size):
        out = ctypes.create_string_buffer(size)
        done = 0

        while done < size:
            chunk = min(MAX_XFER, size - done)
            req = MemReq(pid, chunk, address + done)
            returned = self._call(IOCTL_READ, ctypes.byref(req), ctypes.sizeof(req),
                                  ctypes.byref(out, done), chunk)
            if not returned:
                break
            done += returned

        return out.raw[:done]

    def read_value(self, pid, address, fmt="<Q"):
        import struct
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.read(pid, address, size))[0]

    def write(self, pid, address, data):
        data = bytes(data)
        done = 0

        while done < len(data):
            chunk = min(MAX_XFER, len(data) - done)
            header = ctypes.sizeof(MemReq)
            buf = ctypes.create_string_buffer(header + chunk)
            ctypes.memmove(buf, ctypes.byref(MemReq(pid, chunk, address + done)), header)
            ctypes.memmove(ctypes.addressof(buf) + header, data[done:done + chunk], chunk)

            self._call(IOCTL_WRITE, buf, header + chunk, None, 0)
            done += chunk

    def hide_module(self, pid, module_base):
        req = BaseReq(pid, 0, module_base)
        self._call(IOCTL_HIDE_MODULE, ctypes.byref(req), ctypes.sizeof(req), None, 0)

    def unhide_module(self, pid, module_base):
        req = BaseReq(pid, 0, module_base)
        self._call(IOCTL_UNHIDE_MODULE, ctypes.byref(req), ctypes.sizeof(req), None, 0)
