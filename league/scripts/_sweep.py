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
if not h: raise SystemExit('no device - relaunch the loader')

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

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

local=u64(base+0x1CBB810)
lp=f3((u64(local+0x5BA8) or 0)+0x2E4)
mg=u64(base+0x1CBB7F8)
begin=u64(mg); end=u64(mg+8)
count=(end-begin)//8
print('local=0x%X pos=%s count=%d'%(local,lp,count))

cands=[]
for i in range(count):
    e=u64(begin+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e) or 0
    inmod = base <= vt < base+0x4000000
    t=i32(e+0x1638)
    s=nm(e+0x3F00)
    if t in (1,2) or (inmod and valid(s)):
        cands.append((i,e,vt,t,s))
print('candidates: %d'%len(cands))

reads=0
for i,e,vt,t,s in cands:
    buf=rd(e,0x9000)
    reads+=1
    if not buf: continue
    hits=[]
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        tb=rd(p,0x800)
        reads+=1
        if not tb: continue
        for sub in range(0,0x7F0,4):
            q=struct.unpack_from('<3f',tb,sub)
            if not world(q): continue
            q2=f3(p+sub)
            if q2!=q: continue
            hits.append((off,sub,p,q)); break
        if reads>2500: break
    if not hits: continue
    print('\n[%d] 0x%X vt=0x%X team=%s name=%r inMod=%s'%(
        i,e,vt,t,s,bool(base<=vt<base+0x4000000)))
    for off,sub,p,q in hits[:4]:
        d=math.dist(q,lp) if lp else 0
        print('   +0x%X -> +0x%X (%.1f, %.1f, %.1f) dist=%.0f%s'%(
            off,sub,q[0],q[1],q[2],d,' <== LOCAL' if e==local else ''))
    if reads>2500:
        print('(read budget reached)'); break

k32.CloseHandle(ctypes.c_void_p(h))
