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

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
lp=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
print('local=0x%X pos=%s'%(local,lp))

vts=[]
for i in range(4):
    e=u64(ARR+i*8)
    if not e: continue
    vt=u64(e) or 0
    if vt and base<=vt<base+0x4000000 and vt not in vts:
        vts.append(vt)
print('hero-class vtables from the array: %s'%[hex(v) for v in vts])

CHAINS=(0x5BA8,0x1790,0x2AD0,0x3098,0xC68,0xE0,0x38)
SUBS=(0x2E4,0x504,0x4C4,0x488,0xD4,0x60)

mg=u64(ARR); begin=u64(mg); end=u64(mg+8)
count=(end-begin)//8
print('scanning %d entries'%count)

hits=[]
for i in range(count):
    e=u64(begin+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    if (u64(e) or 0) not in vts: continue
    buf=rd(e,0x2000)
    if not buf: continue
    got=None
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
            got=(off,sub,t); break
        if got: break
    hits.append((i,e,got))

print('\nfound %d object(s) of hero classes'%len(hits))
for i,e,got in hits:
    d=math.dist(got[2],lp) if got else 0
    print('  [%d] 0x%X vt=0x%X team=%s name=%r pos=%s dist=%.0f chain=%s%s'%(
        i,e,u64(e) or 0,i32(e+0x1638),nm(e+0x3F00),got[2] if got else None,d,
        ('+0x%X/+0x%X'%(got[0],got[1])) if got else '-',' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
