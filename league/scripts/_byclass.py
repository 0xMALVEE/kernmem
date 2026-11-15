import ctypes, ctypes.wintypes as w, struct, math

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

h=None
for dev in (r'\\.\VanguardBypass', r'\\?\GLOBALROOT\Device\VanguardBypass'):
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
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def name(a):
    r=rd(a,48)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 500.0<x<20000.0 and 500.0<y<20000.0 and -500.0<z<3000.0

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
lp=f3((u64(local+0x5BA8) or 0)+0x2E4)

known=[0x238D09979C0, local]
vtables=[]
for e in known:
    vt=u64(e)
    if vt: vtables.append(vt)
print('local pos=%s known vtables=%s'%(lp,[hex(v) for v in vtables]))

mg=u64(ARR)
begin=u64(mg) if mg else None
end=u64(mg+8) if mg else None
print('begin=0x%X end=0x%X'%(begin or 0,end or 0))

CHAINS=(0x5BA8,0x1790,0x2AD0,0x3098)
SUBS=(0x2E4,0x790)

print('\n-- all entries whose vtable is a known hero class (past end too) --')
for i in range(0, 0x4000):
    e=u64(begin+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e)
    if vt not in vtables: continue
    nm=name(e+0x3F00)
    got=None
    for co in CHAINS:
        tp=u64(e+co)
        if not tp or tp<0x10000000000 or (base<=tp<base+0x4000000): continue
        for sub in SUBS:
            t=f3(tp+sub)
            if world(t) and f3(tp+sub)==t:
                got=t; break
        if got: break
    d=math.dist(got,lp) if (got and lp) else 0
    print('  [%d] 0x%X vt=0x%X team=%s pos=%s dist=%.0f name=%r%s'%(
        i,e,vt,i32(e+0x1638),got,d,nm,' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
