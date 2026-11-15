import ctypes, ctypes.wintypes as w, struct, math
from collections import defaultdict

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

def nm(a):
    r=rd(a,48)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def valid(s):
    return bool(s) and 2 <= len(s) <= 24 and all(32 <= ord(c) < 127 for c in s)

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 500.0<x<20000.0 and 500.0<y<20000.0 and -500.0<z<3000.0

local=u64(base+0x1CBB810)
lp=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
mg=u64(base+0x1CBB7F8)
begin=u64(mg); end=u64(mg+8)
count=(end-begin)//8
print('local=0x%X pos=%s count=%d'%(local,lp,count))

SUBS=(0x2E4,0x790,0x488,0x504,0x4C4,0xD4)
TASK=0x1000

rows=[]
for i in range(count):
    e=u64(begin+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e) or 0
    if not (base <= vt < base+0x4000000): continue
    buf=rd(e,TASK)
    if not buf: continue
    best=None
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        for sub in SUBS:
            r1=rd(p+sub,12)
            if not r1: continue
            t=struct.unpack('<3f',r1)
            if not world(t): continue
            r2=rd(p+sub,12)
            if not r2 or struct.unpack('<3f',r2)!=t: continue
            best=(off,sub,p,t); break
        if best: break
    if not best: continue
    rows.append((i,e,vt,i32(e+0x1638),nm(e+0x3F00),best))

print('objects with a stable world position: %d'%len(rows))
for i,e,vt,team,name,best in rows:
    off,sub,p,t=best
    d=math.dist(t,lp) if lp else 0
    print('  [%d] 0x%X vt=0x%X team=%s pos=(%.1f,%.1f,%.1f) dist=%.0f name=%r chain+0x%X/+0x%X%s'%(
        i,e,vt,team,t[0],t[1],t[2],d,name,off,sub,' <== LOCAL' if e==local else ''))

print('\n-- grouped by vtable --')
g=defaultdict(list)
for r in rows:
    g[r[2]].append(r)
for vt,items in sorted(g.items(), key=lambda kv:-len(kv[1])):
    teams=[x[3] for x in items]
    print('  vt=0x%X count=%d teams=%s'%(vt,len(items),teams))

k32.CloseHandle(ctypes.c_void_p(h))
