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

def name(a):
    r=rd(a,48)
    if not r: return None
    s=r.decode('utf-16-le','replace').split('\x00')[0]
    return s

def valid(s):
    if not s or len(s)<2 or len(s)>24: return False
    return all(32 <= ord(c) < 127 for c in s)

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
mg=u64(ARR)
begin=u64(mg) if mg else None
end=u64(mg+8) if mg else None
print('local=0x%X manager=0x%X'%(local,mg or 0))
if begin and end and end>begin:
    print('begin=0x%X end=0x%X count=%d'%(begin,end,(end-begin)//8))
else:
    print('manager chain did not resolve; begin=%s end=%s'%(begin,end))

print('\n-- every object with a printable name at +0x3F00 --')
found=[]
for i in range(0, 0x2000):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    s=name(e+0x3F00)
    if not valid(s): continue
    vt=u64(e)
    found.append((i,e,s))
    print('  [%d] 0x%X name=%r team=%s vt=0x%X inMod=%s%s'%(
        i,e,s,i32(e+0x1638),vt or 0,
        bool(vt and base<=vt<base+0x4000000),' <== LOCAL' if e==local else ''))
print('named objects: %d'%len(found))

print('\n-- team histogram across all array slots --')
from collections import Counter
c=Counter()
for i in range(0, 0x2000):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    c[i32(e+0x1638)]+=1
for k,v in c.most_common(8):
    print('  team %s -> %d objects'%(k,v))

k32.CloseHandle(ctypes.c_void_p(h))
