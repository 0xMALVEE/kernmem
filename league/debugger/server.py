"""
Live debugger for the overlay.

Polls the driver in a background thread and serves the raw numbers plus every
projection convention, so the right position offset and matrix convention can be
found by looking instead of guessing. Standard library only.

    python server.py [port]
"""

import ctypes
import ctypes.wintypes as w
import json
import math
import os
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEVICE_PATHS = (r'\\.\VanguardBypass', r'\\?\GLOBALROOT\Device\VanguardBypass')

IOCTL_GET_PID = 0x80002008
IOCTL_READ = 0x80002010
IOCTL_GET_BASE = 0x80002024

# Every value below came from running leagueOffsetDump/pattern-league.txt against
# the live image, then confirming it on six heroes at once. Re-run
# scripts/patterns.py after a patch and update this block.
OFF_LOCAL = 0x1CFA7B0

# Hero list is a struct: +0x8 array pointer, +0x10 count.
OFF_HERO_LIST = 0x1CB8550
OFF_HERO_LIST_ARRAY = 0x8
OFF_HERO_LIST_COUNT = 0x10

OFF_TEAM = 0x164          # byte, 1 = own side, 2 = enemy
OFF_NAME = 0x3890         # ASCII, not UTF-16
OFF_POS = 0xA0            # inline world position
OFF_HP_FROM_POS = 0x64    # current health
OFF_MAXHP_FROM_POS = 0x68 # max health
OFF_RANGE = 0x17080
OFF_RANGE_ALT = 0x3E30

# Older layout, kept so a regression is visible rather than silent.
OFF_ARRAY = 0x1CBB7F8
POS_CANDIDATES = (0x4584, 0x4484)

SIG = b'\x48\x8D\x0D\x00\x00\x00\x00\x0F\x10\x00'
SIG_MASK = [1, 1, 1, 0, 0, 0, 0, 1, 1, 1]

GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
SHARE_ALL = 7
INVALID = ctypes.c_void_p(-1).value

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32 = ctypes.WinDLL('user32', use_last_error=True)

k32.CreateFileW.restype = ctypes.c_void_p
k32.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE]
k32.DeviceIoControl.restype = w.BOOL
k32.DeviceIoControl.argtypes = [ctypes.c_void_p, w.DWORD, ctypes.c_void_p, w.DWORD,
                                ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), w.LPVOID]
k32.CloseHandle.argtypes = [ctypes.c_void_p]
u32.FindWindowW.restype = w.HWND
u32.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u32.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
u32.GetForegroundWindow.restype = w.HWND

# The game window is 4K at 150% here; an unaware process is handed 2545x1354 for a
# 3840x2160 window and every reported coordinate is wrong by the scale factor.
# Opt in before any window call.
try:
    u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))  # PER_MONITOR_AWARE_V2
except Exception:
    try:
        ctypes.WinDLL('shcore').SetProcessDpiAwareness(2)  # PER_MONITOR_DPI_AWARE
    except Exception:
        u32.SetProcessDPIAware()

STATE_LOCK = threading.Lock()
STATE = {'ts': 0.0, 'error': 'starting'}


class Session:
    def __init__(self):
        self.handle = None
        self.pid = 0
        self.base = 0
        self.device = None

    def open(self):
        self.close()
        for path in DEVICE_PATHS:
            h = k32.CreateFileW(path, GENERIC_READ | GENERIC_WRITE, SHARE_ALL, None,
                                OPEN_EXISTING, 0, None)
            if h and h != INVALID:
                self.handle = h
                self.device = path
                return True
        self.handle = None
        return False

    def close(self):
        if self.handle:
            k32.CloseHandle(ctypes.c_void_p(self.handle))
            self.handle = None
            self.device = None

    def ioctl(self, code, payload, outlen):
        out = ctypes.create_string_buffer(outlen)
        returned = w.DWORD(0)
        buf = ctypes.create_string_buffer(payload, len(payload)) if payload else None
        ok = k32.DeviceIoControl(self.handle, code, buf, len(payload) if payload else 0,
                                 out, outlen, ctypes.byref(returned), None)
        return (ok, out.raw[:returned.value]) if ok else (False, None)

    def read(self, address, size):
        if not self.handle or not address or not size:
            return None
        ok, out = self.ioctl(IOCTL_READ, struct.pack('<IIQ', self.pid, size, address), size)
        return out if ok else None

    def u64(self, address):
        raw = self.read(address, 8)
        return struct.unpack('<Q', raw)[0] if raw and len(raw) == 8 else None

    def i32(self, address):
        raw = self.read(address, 4)
        return struct.unpack('<i', raw)[0] if raw and len(raw) == 4 else None

    def f32(self, address):
        raw = self.read(address, 4)
        return struct.unpack('<f', raw)[0] if raw and len(raw) == 4 else None

    def u8(self, address):
        raw = self.read(address, 1)
        return raw[0] if raw else None

    def text(self, address, size):
        raw = self.read(address, size)
        if not raw:
            return ''
        return raw.split(b'\x00')[0].decode('ascii', 'replace')

    def f3(self, address):
        raw = self.read(address, 12)
        return struct.unpack('<3f', raw) if raw and len(raw) == 12 else None

    def resolve(self):
        name = 'League of Legends.exe'.encode('utf-16-le')
        ok, out = self.ioctl(IOCTL_GET_PID, struct.pack('<II', 0, 0) + name.ljust(192, b'\x00'), 8)
        if not ok or not out:
            self.pid = 0
            return False
        self.pid = struct.unpack('<Q', out)[0]
        if not self.pid:
            return False
        ok, out = self.ioctl(IOCTL_GET_BASE, struct.pack('<II', self.pid, 0), 8)
        if not ok or not out:
            return False
        self.base = struct.unpack('<Q', out)[0]
        return bool(self.base)

    def image_size(self):
        head = self.read(self.base, 0x400)
        if not head or head[:2] != b'MZ':
            return 0
        e = struct.unpack_from('<I', head, 0x3C)[0]
        return struct.unpack_from('<I', head, e + 0x18 + 56)[0]

    def find_signature(self):
        size = self.image_size()
        hits = []
        chunk = 0x10000
        for offset in range(0, size, chunk):
            buf = self.read(self.base + offset, min(chunk, size - offset))
            if not buf:
                continue
            i = 0
            while True:
                i = buf.find(SIG[:3], i)
                if i < 0 or i + len(SIG) > len(buf):
                    break
                if all(not m or buf[i + k] == SIG[k] for k, m in enumerate(SIG_MASK)):
                    hits.append(self.base + offset + i)
                i += 1
        targets = []
        for hit in hits:
            raw = self.read(hit + 3, 4)
            if raw:
                targets.append(hit + 7 + struct.unpack('<i', raw)[0])
        return targets


SESSION = Session()


def client_size():
    hwnd = u32.FindWindowW(None, 'League of Legends (TM) Client')
    rect = w.RECT()
    if hwnd and u32.GetClientRect(hwnd, ctypes.byref(rect)):
        return hwnd, rect.right, rect.bottom
    return hwnd, 0, 0


def project(matrix, point, width, height):
    x, y, z = point
    cx = x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12]
    cy = x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13]
    cw = x * matrix[3] + y * matrix[7] + z * matrix[11] + matrix[15]
    if abs(cw) < 1e-4:
        return None
    return [width * 0.5 * (1.0 + cx / cw), height * 0.5 * (1.0 - cy / cw), cw]


def project_col(matrix, point, width, height):
    x, y, z = point
    cx = x * matrix[0] + y * matrix[1] + z * matrix[2] + matrix[3]
    cy = x * matrix[4] + y * matrix[5] + z * matrix[6] + matrix[7]
    cw = x * matrix[12] + y * matrix[13] + z * matrix[14] + matrix[15]
    if abs(cw) < 1e-4:
        return None
    return [width * 0.5 * (1.0 + cx / cw), height * 0.5 * (1.0 - cy / cw), cw]


def multiply(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4))
            for r in range(4) for c in range(4)]


def project_reference(matrix, point, width, height):
    """The older project's exact form: V*P, strided read, and a positive-w gate."""
    x, y, z = point
    cx = x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12]
    cy = x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13]
    cw = x * matrix[3] + y * matrix[7] + z * matrix[11] + matrix[15]
    if cw < 0.1:
        return None
    return [width * 0.5 * (1.0 + cx / cw), height * 0.5 * (1.0 - cy / cw), cw]


def ring_points(center, radius, plane):
    pts = []
    for i in range(48):
        a = 2.0 * math.pi * i / 48
        if plane == 'xz':
            pts.append((center[0] + radius * math.cos(a), center[1], center[2] + radius * math.sin(a)))
        else:
            pts.append((center[0] + radius * math.cos(a), center[1] + radius * math.sin(a), center[2]))
    return pts


def position_candidates(session, obj):
    """Every place a position might live. The AI-manager chain goes first: that is
    where the older project's dump put it, which is why the inline guesses read
    denormals on an unpopulated graph."""
    found = []

    address = obj + OFF_POS
    point = session.f3(address)
    found.append({'offset': OFF_POS, 'chain': None, 'address': address,
                  'value': list(point) if point else None, 'primary': True})

    for off in POS_CANDIDATES:
        address = obj + off
        point = session.f3(address)
        found.append({'offset': off, 'chain': None, 'address': address,
                      'value': list(point) if point else None, 'primary': False})
    return found


def snapshot():
    with STATE_LOCK:
        return dict(STATE)


def poll_once():
    if not SESSION.handle and not SESSION.open():
        return {'ts': time.time(), 'error': 'device not available - is the loader running?'}
    if not SESSION.resolve():
        SESSION.close()
        return {'ts': time.time(), 'error': 'game not running (League of Legends.exe not found)'}

    hwnd, width, height = client_size()
    if not width:
        return {'ts': time.time(), 'error': 'game window not found'}

    base = SESSION.base
    local = SESSION.u64(base + OFF_LOCAL)
    targets = SESSION.find_signature()
    camera = targets[0] if targets else None

    state = {
        'ts': time.time(),
        'pid': SESSION.pid,
        'base': base,
        'device': SESSION.device,
        'client': [width, height],
        'focused': u32.GetForegroundWindow() == hwnd,
        'local': local,
        'readable': local is not None,
        'signature': [t - base for t in targets],
        'camera': (camera - base) if camera else None,
        'heroes': [],
        'positions': [],
        'conventions': [],
        'name': '',
        'team': None,
        'hp': None,
        'maxHp': None,
        'localPosition': [],
        'heroListCount': 0,
        'error': None,
    }

    if local:
        state['positions'] = position_candidates(SESSION, local)
        state['team'] = SESSION.u8(local + OFF_TEAM)
        state['name'] = SESSION.text(local + OFF_NAME, 32)
        state['localPosition'] = list(SESSION.f3(local + OFF_POS) or [])
        state['hp'] = SESSION.f32(local + OFF_POS + OFF_HP_FROM_POS)
        state['maxHp'] = SESSION.f32(local + OFF_POS + OFF_MAXHP_FROM_POS)

        measured = SESSION.f32(local + OFF_RANGE)
        if not (isinstance(measured, float) and 100.0 < measured < 1500.0):
            measured = SESSION.f32(local + OFF_RANGE_ALT)
        state['range'] = measured

    list_ptr = SESSION.u64(base + OFF_HERO_LIST)
    array = SESSION.u64(list_ptr + OFF_HERO_LIST_ARRAY) if list_ptr else None
    count = SESSION.i32(list_ptr + OFF_HERO_LIST_COUNT) if list_ptr else 0
    state['heroListCount'] = count or 0

    for i in range(min(count or 0, 16)):
        obj = SESSION.u64(array + i * 8) if array else None
        if not obj:
            continue

        team = SESSION.u8(obj + OFF_TEAM)
        if team not in (1, 2):
            continue

        position = SESSION.f3(obj + OFF_POS)
        if position and (abs(position[0]) < 1.0 or abs(position[1]) < 1.0):
            position = None

        state['heroes'].append({
            'index': i,
            'address': obj - base,
            'team': team,
            'name': SESSION.text(obj + OFF_NAME, 32),
            'hp': SESSION.f32(obj + OFF_POS + OFF_HP_FROM_POS),
            'maxHp': SESSION.f32(obj + OFF_POS + OFF_MAXHP_FROM_POS),
            'position': {'offset': OFF_POS, 'value': list(position)} if position else None,
            'isLocal': obj == local,
        })

    if camera and local:
        raw = SESSION.read(camera, 0x80)
        local_pos = SESSION.f3(local + OFF_POS)
        if raw and local_pos:
            view = list(struct.unpack_from('<16f', raw, 0))
            proj = list(struct.unpack_from('<16f', raw, 0x40))
            measured = state.get('range')
            radius = measured if (isinstance(measured, (int, float)) and 100.0 < measured < 1500.0) else 700.0
            state['view'] = view
            state['projection'] = proj
            state['viewCrc'] = sum(raw) % 0xFFFFFFFF

            for label, matrix, projector, plane in (
                ('row V*P xy', multiply(view, proj), project, 'xy'),
                ('row V*P xz', multiply(view, proj), project, 'xz'),
                ('REFERENCE w>0', multiply(view, proj), project_reference, 'xy'),
                ('row P*V xy', multiply(proj, view), project, 'xy'),
                ('col V*P xy', multiply(view, proj), project_col, 'xy'),
                ('col P*V xy', multiply(proj, view), project_col, 'xy'),
                ('swapped: P first xy', multiply(proj, view), project, 'xz'),
            ):
                self_screen = projector(matrix, local_pos, width, height)
                if not self_screen:
                    state['conventions'].append({'name': label, 'self': None, 'ring': [], 'heroes': []})
                    continue
                pts = []
                for p in ring_points(local_pos, radius, plane):
                    s = projector(matrix, p, width, height)
                    if s:
                        pts.append(s)
                state['conventions'].append({
                    'name': label,
                    'self': self_screen,
                    'ring': pts,
                    'heroes': [{'name': h['name'], 'screen': projector(matrix, h['position']['value'], width, height)}
                               for h in state['heroes'] if h['position']],
                })

    return state


def poller():
    while True:
        try:
            state = poll_once()
        except Exception as exc:  # noqa: BLE001 - surface anything to the UI
            state = {'ts': time.time(), 'error': repr(exc)}
        with STATE_LOCK:
            STATE.clear()
            STATE.update(state)
        time.sleep(0.2)


def scan():
    """Brute-force every plausible position triple in the local object."""
    if not SESSION.handle and not SESSION.open():
        return {'error': 'device not available'}
    if not SESSION.pid and not SESSION.resolve():
        return {'error': 'game not running'}

    hwnd, width, height = client_size()
    local = SESSION.u64(SESSION.base + OFF_LOCAL)
    targets = SESSION.find_signature()
    if not local or not targets or not width:
        return {'error': 'not ready'}

    raw = SESSION.read(targets[0], 0x80)
    if not raw:
        return {'error': 'camera unreadable'}
    matrix = multiply(list(struct.unpack_from('<16f', raw, 0)),
                      list(struct.unpack_from('<16f', raw, 0x40)))

    buf = SESSION.read(local, 0x9000)
    if not buf:
        return {'error': 'object unreadable'}

    cx, cy = width * 0.5, height * 0.5
    rows = []
    for off in range(0, len(buf) - 12, 4):
        x, y, z = struct.unpack_from('<3f', buf, off)
        if not all(v == v and abs(v) < 1e6 for v in (x, y, z)):
            continue
        if not (100.0 < x < 30000.0 and 100.0 < y < 30000.0 and -4000.0 < z < 4000.0):
            continue
        s = project(matrix, (x, y, z), width, height)
        if not s:
            continue
        offc = math.hypot(s[0] - cx, s[1] - cy)
        if offc > 900:
            continue
        rows.append({'offset': off, 'value': [x, y, z], 'screen': s, 'offCentre': offc})

    rows.sort(key=lambda r: r['offCentre'])
    return {'client': [width, height], 'rows': rows[:40]}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, payload):
        body = json.dumps(payload, default=str).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith('/api/state'):
            self._send(snapshot())
        elif self.path.startswith('/api/scan'):
            self._send(scan())
        else:
            self.send_response(404)
            self.end_headers()


def main():
    port = int(os.environ.get('VGB_DEBUG_PORT', '8777'))
    threading.Thread(target=poller, daemon=True).start()
    print('debugger api on http://127.0.0.1:%d/api/state' % port, flush=True)
    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    main()
