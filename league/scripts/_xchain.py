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

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x3000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 200.0<x<20000.0 and 200.0<y<20000.0 and -500.0<z<3000.0

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)

objs=[]
for i in range(10):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e)
    if not vt or not (base<=vt<base+0x3000000): continue
    objs.append((i,e))
print('objects: %s'%[('0x%X'%e) for _,e in objs])

others=[e for _,e in objs if e!=local]
print('local=0x%X others=%s'%(local,[hex(e) for e in others]))

# cross scan: same offset must be a pool pointer on local and on an other,
# and each chained triple must be a distinct plausible world position.
for other in others:
    lb=rd(local,0x6000); ob=rd(other,0x6000)
    if not lb or not ob:
        print('  0x%X unreadable'%other); continue
    print('\n== vs 0x%X =='%other)
    for off in range(0,min(len(lb),len(ob))-8,8):
        lp=struct.unpack_from('<Q',lb,off)[0]
        op=struct.unpack_from('<Q',ob,off)[0]
        if not (pool(lp) and pool(op)): continue
        a=f3(lp+0x2E4); b=f3(op+0x2E4)
        if not (world(a) and world(b)): continue
        d=math.dist(a,b)
        if d<1.0 or d>20000.0: continue
        print('  +0x%04X local=(%.1f, %.1f, %.1f) other=(%.1f, %.1f, %.1f) dist=%.1f'%(
            off,a[0],a[1],a[2],b[0],b[1],b[2],d))

print('\n-- health pair after the local position --')
ptr=u64(local+0x5BA8)
print('ptr=0x%X'%(ptr or 0))
if ptr:
    raw=rd(ptr+0x2E4,0x100)
    for i in range(0,len(raw)-8,4):
        a,b=struct.unpack_from('<2f',raw,i)
        if a==a and b==b and 0.0<=a<=b and 50.0<b<6000.0:
            print('  hp pair at pos+0x%X  a=%.2f b=%.2f'%(i,a,b))
    print('  first 16 floats after pos: %s'%([round(struct.unpack_from('<f',raw,i)[0],2) for i in range(0,64,4)]))

k32.CloseHandle(ctypes.c_void_p(h))
