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
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

def nm(a):
    r=rd(a,48)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 500.0<x<20000.0 and 500.0<y<20000.0 and -500.0<z<3000.0

def health(pair):
    if not pair: return False
    cur,mx=pair
    if not (cur==cur and mx==mx): return False
    return 50.0 < mx < 5000.0 and 0.0 <= cur <= mx + 1.0

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
lp=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
mg=u64(ARR); begin=u64(mg); end=u64(mg+8)
count=(end-begin)//8
print('local=0x%X pos=%s count=%d'%(local,lp,count))

CHAINS=(0x5BA8,0xC68,0x1790,0x2AD0,0x3098,0xE0,0x38)
SUBS=(0x2E4,0x488,0x504,0x4C4,0xD4,0x60)

cands=[]
for i in range(count):
    e=u64(begin+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    if i32(e+0x1638) in (1,2):
        cands.append((i,e))
print('team-1/2 candidates: %d'%len(cands))

print('\n-- qualifying objects: stable position AND plausible health pair --')
qualified=0
for i,e in cands:
    buf=rd(e,0x3000)
    if not buf: continue
    got=None
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        tb=rd(p,0x900)
        if not tb: continue
        for sub in SUBS:
            t=struct.unpack_from('<3f',tb,sub)
            if not world(t): continue
            r2=rd(p+sub,12)
            if not r2 or struct.unpack('<3f',r2)!=t: continue
            hp=rd(p+sub+0x98,8)
            pair=struct.unpack('<2f',hp) if hp else None
            if not health(pair): continue
            got=(off,sub,t,pair); break
        if got: break
    if not got: continue
    off,sub,t,pair=got
    qualified+=1
    d=math.dist(t,lp)
    print('  [%d] 0x%X vt=0x%X team=%d name=%r pos=(%.1f,%.1f,%.1f) hp=%.1f/%.1f dist=%.0f chain=+0x%X/+0x%X%s'%(
        i,e,u64(e) or 0,i32(e+0x1638),nm(e+0x3F00),t[0],t[1],t[2],pair[0],pair[1],d,off,sub,
        ' <== LOCAL' if e==local else ''))

print('qualified: %d'%qualified)

k32.CloseHandle(ctypes.c_void_p(h))
