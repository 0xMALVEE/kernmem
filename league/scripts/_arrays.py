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

def nm(a):
    r=rd(a,48)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def valid(s):
    return bool(s) and 2 <= len(s) <= 24 and all(32 <= ord(c) < 127 for c in s)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 500.0<x<20000.0 and 500.0<y<20000.0 and -500.0<z<3000.0

local=u64(base+0x1CBB810)
print('local=0x%X'%(local or 0))

CHAINS=(0x5BA8,0x1790,0x2AD0,0x3098)
SUBS=(0x2E4,0x790)

print('\n-- globals pointing at arrays with 2+ hero-class entries --')
seen=set()
for off in range(0x1CA0000, 0x1D20000, 8):
    v=u64(base+off)
    if not v or v<0x10000000000 or v>0x7FFFFFFFFFFF: continue
    if base <= v < base+0x4000000: continue
    if v in seen: continue
    buf=rd(v,0x200)
    if not buf: continue
    heroes=[]
    for k in range(0,len(buf),8):
        e=struct.unpack_from('<Q',buf,k)[0]
        if not e or e<0x10000000000 or e>0x7FFFFFFFFFFF: continue
        vt=u64(e)
        if not vt or not (base<=vt<base+0x4000000): continue
        t=i32(e+0x1638)
        if t not in (1,2): continue
        got=None
        for co in CHAINS:
            tp=u64(e+co)
            if not tp or tp<0x10000000000 or (base<=tp<base+0x4000000): continue
            for sub in SUBS:
                q=f3(tp+sub)
                if world(q) and f3(tp+sub)==q:
                    got=q; break
            if got: break
        heroes.append((k,e,t,got,nm(e+0x3F00)))
    if len(heroes) >= 2:
        seen.add(v)
        print('  base+0x%X -> 0x%X heroes=%d'%(off,v,len(heroes)))
        for k,e,t,pos,name in heroes:
            print('     [+0x%X] 0x%X team=%s pos=%s name=%r%s'%(
                k,e,t,pos,name,' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
