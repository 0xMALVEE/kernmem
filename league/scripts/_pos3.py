import ctypes, ctypes.wintypes as w, struct, math
from collections import defaultdict

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

def asc(a,n=32):
    r=rd(a,n)
    if not r: return ''
    return r.split(b'\x00')[0].decode('ascii','replace')

def b1(a):
    r=rd(a,1)
    return r[0] if r else None

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 300.0<x<20000.0 and 300.0<y<20000.0 and -800.0<z<3000.0

lst=u64(base+OFF_LIST)
arr=struct.unpack('<Q',rd(lst+8,8))[0]
cnt=struct.unpack('<i',rd(lst+0x10,4))[0]
local=u64(base+OFF_LOCAL)
print('count=%d local=0x%X'%(cnt,local or 0))

heroes=[]
for k in range(min(cnt,12)):
    e=u64(arr+k*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    heroes.append((k,e,asc(e+OFF_NAME),b1(e+OFF_TEAM)))
    print('  [%d] 0x%X team=%s %r%s'%(k,e,b1(e+OFF_TEAM),asc(e+OFF_NAME),' <== LOCAL' if e==local else ''))

SUBS=(0x25C,0x2E4,0x2E0,0x264,0x504,0x4D4,0x70,0x28,0x60,0xA0,0x2E8,0x258)
shared=defaultdict(list)

for k,e,name,team in heroes:
    buf=rd(e,0x4000)
    if not buf: continue
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        tb=rd(p,0x800)
        if not tb: continue
        for sub in SUBS:
            if sub+12 > len(tb): continue
            t=struct.unpack_from('<3f',tb,sub)
            if world(t):
                shared[(off,sub)].append((k,name,team,t))
                break

print('\n=== offsets valid on 2+ heroes ===')
for (off,sub),items in sorted(shared.items(), key=lambda kv:-len(kv[1])):
    if len(items) < 2: continue
    print('  +0x%X/+0x%X  %d heroes %s'%(off,sub,len(items),[(i[0],i[1],i[2]) for i in items]))
    for k,name,team,t in items:
        print('      [%d] %r team=%s (%.1f, %.1f, %.1f)'%(k,name,team,t[0],t[1],t[2]))

print('\n=== single-hero offsets (top 15) ===')
for (off,sub),items in sorted(shared.items(), key=lambda kv:-len(kv[1]))[:15]:
    print('  +0x%X/+0x%X %d heroes'%(off,sub,len(items)))

k32.CloseHandle(ctypes.c_void_p(h))
