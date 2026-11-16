import ctypes, ctypes.wintypes as w, struct, math

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

OFF_LOCAL=0x1CFA7B0
OFF_LIST=0x1CB8550
OFF_TEAM=0x164
OFF_NAME=0x3890
OFF_POS=0x25C

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

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 300.0<x<20000.0 and 300.0<y<20000.0 and -800.0<z<3000.0

local=u64(base+OFF_LOCAL)
print('local=0x%X'%(local or 0))
if not local: raise SystemExit('local null')

print('\n-- raw qwords of local, first 0x80 --')
buf=rd(local,0x80)
for i in range(0,len(buf),8):
    v=struct.unpack_from('<Q',buf,i)[0]
    tag=' pool' if pool(v) else (' image' if base<=v<base+0x4000000 else '')
    print('  +0x%02X 0x%016X%s'%(i,v,tag))

print('\n-- every pool pointer -> any sub-offset with a stable plausible triple --')
big=rd(local,0x4000)
hits=0
for off in range(0,len(big)-8,8):
    p=struct.unpack_from('<Q',big,off)[0]
    if not pool(p): continue
    tb=rd(p,0x1000)
    if not tb: continue
    for sub in range(0,0xFE0,4):
        t=struct.unpack_from('<3f',tb,sub)
        if not world(t): continue
        r2=rd(p+sub,12)
        if not r2 or struct.unpack('<3f',r2)!=t: continue
        print('  +0x%X -> +0x%X (%.1f, %.1f, %.1f)'%(off,sub,t[0],t[1],t[2]))
        hits+=1
        break
    if hits>10: break
print('hits=%d'%hits)

print('\n-- list container detail --')
lst=u64(base+OFF_LIST)
print('list=0x%X'%(lst or 0))
if lst:
    lb=rd(lst,0x80)
    for i in range(0,len(lb),8):
        v=struct.unpack_from('<Q',lb,i)[0]
        tag=' pool' if pool(v) else ''
        print('  +0x%02X 0x%016X%s'%(i,v,tag))

k32.CloseHandle(ctypes.c_void_p(h))
