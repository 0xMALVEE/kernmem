import ctypes, ctypes.wintypes as w, struct, math

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

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
pid=struct.unpack('<Q',o)[0]
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0]

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r else None

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('local=0x%X team@0x1638=%s'%(local,i32(local+0x1638)))

other=None
for i in range(10):
    e=u64(ARR+i*8)
    if not e or e==local: continue
    if i32(e+0x1638) not in (1,2): continue
    other=e; break
print('other=0x%X team=%s'%(other or 0, i32(other+0x1638) if other else None))
if not other: raise SystemExit('no second hero')

def plaus(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e7 for v in t): return False
    return (200.0<x<20000.0 and 200.0<y<20000.0 and -500.0<z<3000.0)

SIZE=0x6000
localbuf=rd(local,SIZE)
print('local bytes %d'%len(localbuf))

# candidate sub-offsets inside a manager object, from the old project plus neighbours
SUBS=(0x25C,0x260,0x264,0xF8,0xFC,0x100,0x2E0,0x2E4)

print('\n-- pointer + suboffset search, local vs other --')
for off in range(0,SIZE-8,8):
    ptr=struct.unpack_from('<Q',localbuf,off)[0]
    if not (0x10000000000 < ptr < 0x7FFFFFFFFFFF): continue
    if base <= ptr < base+0x3000000: continue
    for sub in SUBS:
        a=f3(ptr+sub)
        if not plaus(a): continue
        b=f3((u64(other+off) or 0)+sub) if u64(other+off) else None
        if not plaus(b): continue
        d=math.dist(a,b)
        if d<1.0 or d>20000.0: continue
        print('  +0x%04X -> +0x%X  A=(%.1f, %.1f, %.1f) B=(%.1f, %.1f, %.1f) dist=%.1f'%(
            off,sub,a[0],a[1],a[2],b[0],b[1],b[2],d))

print('\n-- stability re-read of hits --')
for off in range(0,SIZE-8,8):
    ptr=struct.unpack_from('<Q',localbuf,off)[0]
    if not (0x10000000000 < ptr < 0x7FFFFFFFFFFF) or base <= ptr < base+0x3000000: continue
    for sub in SUBS:
        a1=f3(ptr+sub)
        if not plaus(a1): continue
        a2=f3(ptr+sub)
        if a1==a2:
            print('  +0x%04X -> +0x%X stable (%.1f, %.1f, %.1f)'%(off,sub,a1[0],a1[1],a1[2]))

k32.CloseHandle(ctypes.c_void_p(h))
