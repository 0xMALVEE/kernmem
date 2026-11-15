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
print('list=0x%X array=0x%X count=%d local=0x%X'%(lst,arr,cnt,local or 0))

heroes=[]
for k in range(min(cnt,12)):
    e=u64(arr+k*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    heroes.append((k,e,asc(e+OFF_NAME),b1(e+OFF_TEAM)))
    print('  [%d] 0x%X team=%s name=%r%s'%(k,e,b1(e+OFF_TEAM),asc(e+OFF_NAME),' <== LOCAL' if e==local else ''))

print('\n=== scanning each object + pointer targets for stable plausible triples ===')
from collections import defaultdict
shared=defaultdict(list)

for k,e,name,team in heroes:
    buf=rd(e,0x6000)
    if not buf: continue
    found=[]
    # direct on the object
    for off in range(0,len(buf)-12,4):
        t=struct.unpack_from('<3f',buf,off)
        if not world(t): continue
        if struct.unpack_from('<3f',buf,off)!=t: continue
        found.append((None,off,t))
    # through pointers
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        tb=rd(p,0x1000)
        if not tb: continue
        for sub in range(0,0xFE0,4):
            t=struct.unpack_from('<3f',tb,sub)
            if not world(t): continue
            found.append((off,sub,t))
    print('  [%d] %r team=%s candidates=%d'%(k,name,team,len(found)))
    for off,sub,t in found[:6]:
        key=(off,sub)
        shared[key].append((k,name,team,t))
        print('     %s(%.1f, %.1f, %.1f)'%(('+0x%X/+0x%X '%(off,sub)) if off is not None else '+0x%X direct '%sub,t[0],t[1],t[2]))

print('\n=== offsets shared by 2+ heroes ===')
for (off,sub),items in sorted(shared.items(), key=lambda kv:-len(kv[1])):
    if len(items)<2: continue
    label = ('+0x%X/+0x%X'%(off,sub)) if off is not None else '+0x%X direct'%sub
    print('  %s  %d heroes: %s'%(label,len(items),[(i[0],i[1]) for i in items]))

k32.CloseHandle(ctypes.c_void_p(h))
