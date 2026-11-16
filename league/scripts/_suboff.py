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

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def plausible(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 500.0<x<20000.0 and 500.0<y<20000.0 and -500.0<z<3000.0

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
lptr=u64(local+0x5BA8)
lpos=f3(lptr+0x2E4)
print('local pos=%s'%(lpos,))

# is there also an inline triple on the local hero that matches?
print('local inline 0x61E4 = %s'%(f3(local+0x61E4),))
print('local chain+0x2E4 = %s'%(f3(lptr+0x2E4),))

for i in range(0x400):
    e=u64(ARR+i*8)
    if not e or e==local: continue
    if i32(e+0x1638) not in (1,2): continue
    nm=rd(e+0x3F00,32)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    print('\n[%d] 0x%X team=%d %r'%(i,e,i32(e+0x1638),nm))

    targets=[]
    for co in (0x5BA8,0x1790,0x2AD0,0x3098):
        tp=u64(e+co)
        if tp and tp>0x10000000000 and not (base<=tp<base+0x4000000):
            targets.append(('+0x%X'%co,tp))

    for label,tp in targets:
        buf=rd(tp,0x800)
        if not buf:
            print('  %s -> 0x%X unreadable'%(label,tp)); continue
        hits=[]
        for off in range(0,len(buf)-12,4):
            t=struct.unpack_from('<3f',buf,off)
            if not plausible(t): continue
            t2=f3(tp+off)
            if t2!=t: continue
            hits.append((off,t))
        print('  %s -> 0x%X  plausible stable triples: %d'%(label,tp,len(hits)))
        for off,t in hits[:6]:
            d=math.dist(t,lpos) if lpos else 0
            print('     +0x%X (%.1f, %.1f, %.1f) distFromLocal=%.0f'%(off,t[0],t[1],t[2],d))

    # inline triples on the object itself
    ibuf=rd(e,0x8000)
    ihits=[]
    if ibuf:
        for off in range(0,len(ibuf)-12,4):
            t=struct.unpack_from('<3f',ibuf,off)
            if not plausible(t): continue
            ihits.append((off,t))
    print('  inline plausible triples: %d %s'%(len(ihits),[(hex(o),tuple(round(v,1) for v in t)) for o,t in ihits[:4]]))

k32.CloseHandle(ctypes.c_void_p(h))
