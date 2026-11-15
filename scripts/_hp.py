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

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def f2(a):
    r=rd(a,8)
    return struct.unpack('<2f',r) if r and len(r)==8 else None

def f1(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r and len(r)==4 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('local=0x%X'%local)

objs=[]
for i in range(10):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e)
    if not vt or not (base<=vt<base+0x3000000): continue
    nm=rd(e+0x3F00,32)
    objs.append((i,e,nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''))

for i,e,nm in objs:
    ptr=u64(e+0x5BA8)
    team=i32(e+0x1638)
    print('\n[%d] 0x%X %r team=%s ptr=0x%X'%(i,e,nm,team,ptr or 0))
    if not ptr or base<=ptr<base+0x3000000:
        print('   ptr not a pool pointer; scanning for one')
        buf=rd(e,0x6000)
        best=None
        for off in range(0,len(buf)-8,8):
            cand=struct.unpack_from('<Q',buf,off)[0]
            if not (0x10000000000<cand<0x7FFFFFFFFFFF) or base<=cand<base+0x3000000: continue
            q=f3(cand+0x2E4)
            if not q: continue
            if not (200.0<q[0]<20000.0 and 200.0<q[1]<20000.0 and -500.0<q[2]<3000.0): continue
            q2=f3(cand+0x2E4)
            if q2!=q: continue
            best=(off,cand,q); break
        if not best:
            print('   no chain found'); continue
        print('   found +0x%X -> 0x%X %s'%(best[0],best[1],best[2]))
        ptr=best[1]
    pos=f3(ptr+0x2E4)
    print('   pos=%s'%(pos,))
    if not pos: continue
    print('   hp pairs around +0x2E4: %s %s %s'%(f2(ptr+0x320),f2(ptr+0x324),f2(ptr+0x328)))
    print('   ints at ptr+0x320..0x340: %s'%([i32(ptr+o) for o in range(0x320,0x344,4)]))

print('\n-- attack range candidates on local: floats 300..900 in first 0x4000 --')
buf=rd(local,0x4000)
for off in range(0,len(buf)-4,4):
    v=struct.unpack_from('<f',buf,off)[0]
    if 300.0<v<900.0 and v==v:
        v2=f1(local+off)
        if v2==v:
            print('  +0x%X = %.2f'%(off,v))

k32.CloseHandle(ctypes.c_void_p(h))
