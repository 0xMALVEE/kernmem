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
pid=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
if not pid: raise SystemExit('game not running')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
print('pid=%d base=0x%X'%(pid,base))

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
    r=rd(a,64)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def valid(s):
    return bool(s) and 2 <= len(s) <= 20 and all(32 <= ord(c) < 127 for c in s)

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 300.0<x<20000.0 and 300.0<y<20000.0 and -800.0<z<3000.0

def health(pair):
    if not pair: return False
    cur,mx=pair
    if not (cur==cur and mx==mx): return False
    return 20.0 < mx < 8000.0 and -1.0 <= cur <= mx + 1.0

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
mg=u64(ARR); begin=u64(mg); end=u64(mg+8)
print('local=0x%X manager=0x%X count=%s'%(local or 0, mg or 0,
      (end-begin)//8 if begin and end and end>begin else 'n/a'))

print('\n=== every named object in the list ===')
named=[]
if begin and end and end>begin:
    for k in range(min((end-begin)//8, 0x2000)):
        e=u64(begin+k*8)
        if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
        s=nm(e+0x3F00)
        if not valid(s): continue
        named.append((k,e,s))
        print('  [%d] 0x%X name=%r%s'%(k,e,s,' <== LOCAL' if e==local else ''))
print('named: %d'%len(named))

if len(named) >= 2:
    a=named[0][1]; b=named[-1][1]
    print('\n=== team offsets between %r and %r ==='%(named[0][2],named[-1][2]))
    for off in range(0,0x4000,4):
        av=i32(a+off); bv=i32(b+off)
        if av is None or bv is None: continue
        if {av,bv}=={1,2}:
            print('  +0x%X  %s=%d  %s=%d'%(off,named[0][2],av,named[-1][2],bv))

print('\n=== per-named position discovery ===')
for k,e,s in named:
    buf=rd(e,0x3000)
    if not buf: continue
    best=None
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        tb=rd(p,0x900)
        if not tb: continue
        for sub in range(0,0x8F0,4):
            t=struct.unpack_from('<3f',tb,sub)
            if not world(t): continue
            r2=rd(p+sub,12)
            if not r2 or struct.unpack('<3f',r2)!=t: continue
            hp=rd(p+sub+0x98,8)
            pair=struct.unpack('<2f',hp) if hp else None
            if health(pair):
                best=(off,sub,t,pair); break
        if best: break
    if best:
        off,sub,t,pair=best
        print('  [%d] %r -> +0x%X/+0x%X (%.1f, %.1f, %.1f) hp=%.1f/%.1f'%(
            k,s,off,sub,t[0],t[1],t[2],pair[0],pair[1]))
    else:
        print('  [%d] %r -> no position+health'%(k,s))

k32.CloseHandle(ctypes.c_void_p(h))
