import ctypes, ctypes.wintypes as w, struct

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

CHUNK=0x10000

h=None
for dev in (r'\\.\Kernmem', r'\\?\GLOBALROOT\Device\Kernmem'):
    hh=k32.CreateFileW(dev,GR|GW,SA,None,OE,0,None)
    if hh and hh!=INV:
        h=hh; break
if not h: raise SystemExit('no device')

def ioctl(c,i,n):
    o=ctypes.create_string_buffer(n); r=w.DWORD(0)
    b=ctypes.create_string_buffer(i,len(i)) if i else None
    ok=k32.DeviceIoControl(h,c,b,len(i) if i else 0,o,n,ctypes.byref(r),None)
    return ok,o.raw[:r.value]

_,o=ioctl(0x80002008,struct.pack('<II',0,0)+'League of Legends.exe'.encode('utf-16-le').ljust(192,b'\x00'),8)
pid=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
if not pid: raise SystemExit('game not running')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
print('pid=%d base=0x%X'%(pid,base))

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

hdr=rd(base,0x400)
if not hdr or hdr[:2]!=b'MZ': raise SystemExit('bad header')
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]

img=bytearray()
off=0
while off < SIZE:
    want=min(CHUNK, SIZE-off)
    b=rd(base+off, want)
    if not b:
        img += b'\x00'*want
    else:
        img += b if len(b)==want else b + b'\x00'*(want-len(b))
    off += want
img=bytes(img)
print('image %d bytes'%len(img))


def parse(sig):
    parts=sig.split()
    return bytes(0 if p=='?' else int(p,16) for p in parts), [p!='?' for p in parts]


def longest_run(mask):
    best_start = best_len = 0
    run = None
    for i, m in enumerate(list(mask) + [False]):
        if m:
            if run is None:
                run = i
        else:
            if run is not None:
                if i - run > best_len:
                    best_len = i - run
                    best_start = run
                run = None
    return best_start, best_len


def scan(name, sig):
    pat, mask = parse(sig)
    a_start, a_len = longest_run(mask)
    if not a_len:
        print('\n%s  no fixed bytes'%name)
        return []
    anchor = pat[a_start:a_start + a_len]
    out = []
    at = 0
    while True:
        i = img.find(anchor, at)
        if i < 0:
            break
        pos = i - a_start
        if pos >= 0 and pos + len(pat) <= len(img):
            ok = True
            for k, m in enumerate(mask):
                if m and img[pos + k] != pat[k]:
                    ok = False
                    break
            if ok:
                out.append(pos)
        at = i + 1
    print('\n%s  hits=%d  %s' % (name, len(out), [hex(x) for x in out[:8]]))
    return out


res = {}
for name, sig in (
    ('oLocalPlayer', '48 8B 0D ? ? ? ? 4C ? ? 74 ? 49'),
    ('oHerroList', '48 8B 0D ? ? ? ? 0F 85 ? ? ? ? 83'),
):
    for i in scan(name, sig):
        tgt = i + 7 + struct.unpack_from('<i', img, i + 3)[0]
        if not (0 <= tgt < SIZE):
            print('   at 0x%X -> 0x%X (outside image)' % (i, tgt))
            continue
        print('   at 0x%X -> global base+0x%X' % (i, tgt))
        res[name] = tgt

for name, sig in (
    ('TeamID', '0F B6 88 ? ? ? ? EB 07 0F B6 8E'),
    ('RangeAttack', 'F3 0F 10 80 ? ? ? ? F3 0F 11 47'),
    ('NamePlayer', '48 81 C1 ? ? ? ? 48 3B CA 74 ? 48 83 7A ? ? 76'),
):
    for i in scan(name, sig):
        off = struct.unpack_from('<i', img, i + 3)[0]
        print('   at 0x%X -> field +0x%X' % (i, off))
        res.setdefault(name, off)

print('\n=== resolved ===')
for k, v in res.items():
    print('  %s = 0x%X' % (k, v))

if 'oLocalPlayer' in res:
    local = struct.unpack('<Q', rd(base + res['oLocalPlayer'], 8) or b'\0' * 8)[0]
    print('\nlocal: 0x%X' % local)
    if local:
        if 'TeamID' in res:
            b = rd(local + res['TeamID'], 1)
            print('  team byte +0x%X = %s' % (res['TeamID'], b[0] if b else None))
        if 'RangeAttack' in res:
            f = rd(local + res['RangeAttack'], 4)
            print('  range +0x%X = %s' % (res['RangeAttack'], struct.unpack('<f', f)[0] if f else None))
        if 'NamePlayer' in res:
            nm = rd(local + res['NamePlayer'], 48)
            print('  name +0x%X = %r' % (res['NamePlayer'], nm.decode('utf-16-le', 'replace').split('\x00')[0] if nm else None))
        p = rd(local + 0x25C, 12)
        print('  position +0x25C = %s' % (struct.unpack('<3f', p) if p else None))

if 'oHerroList' in res:
    lst = struct.unpack('<Q', rd(base + res['oHerroList'], 8) or b'\0' * 8)[0]
    print('\nherolist: 0x%X' % lst)
    if lst:
        for k in range(20):
            raw = rd(lst + k * 8, 8)
            if not raw:
                continue
            e = struct.unpack('<Q', raw)[0]
            if not e or e < 0x10000000000:
                continue
            nm = rd(e + res.get('NamePlayer', 0x68), 48)
            nm = nm.decode('utf-16-le', 'replace').split('\x00')[0] if nm else ''
            tb = rd(e + res.get('TeamID', 0x259), 1)
            p = rd(e + 0x25C, 12)
            print('  [%d] 0x%X team=%s pos=%s name=%r%s' % (
                k, e, tb[0] if tb else None,
                struct.unpack('<3f', p) if p else None, nm,
                ' <== LOCAL' if e == local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
