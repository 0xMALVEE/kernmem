import ctypes, ctypes.wintypes as w, struct, math, time

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
OFF_POS=0xA0

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

lst=u64(base+OFF_LIST)
arr=struct.unpack('<Q',rd(lst+8,8))[0]
cnt=struct.unpack('<i',rd(lst+0x10,4))[0]
local=u64(base+OFF_LOCAL)
print('count=%d local=0x%X'%(cnt,local or 0))

print('\n-- chain ptr delta vs obj, and position at obj+0xA0 --')
for k in range(min(cnt,12)):
    e=u64(arr+k*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    ch=u64(e+0x200)
    delta = (ch-e) if ch else None
    p=rd(e+OFF_POS,12)
    pp=struct.unpack('<3f',p) if p else None
    time.sleep(0.3)
    p2=rd(e+OFF_POS,12)
    pp2=struct.unpack('<3f',p2) if p2 else None
    print('  [%d] %-10r team=%s chainDelta=%s pos=%s moved=%.1f%s'%(
        k,asc(e+OFF_NAME),b1(e+OFF_TEAM),
        hex(delta) if delta is not None else None,pp,
        math.dist(pp,pp2) if (pp and pp2) else -1,' <== LOCAL' if e==local else ''))

print('\n-- health pair hunt around obj+0xA0 (local) --')
for off in range(-0x30,0x40,4):
    r=rd(local+OFF_POS+off,8)
    if not r: continue
    a,b=struct.unpack('<2f',r)
    if a==a and b==b and 0.0<=a<=b and 50.0<b<6000.0:
        print('  pos%+d  %.1f / %.1f'%(off,a,b))

print('\n-- same hunt on Fizz --')
for k in range(min(cnt,12)):
    e=u64(arr+k*8)
    if not e or asc(e+OFF_NAME)!='Fizz': continue
    for off in range(-0x30,0x40,4):
        r=rd(e+OFF_POS+off,8)
        if not r: continue
        a,b=struct.unpack('<2f',r)
        if a==a and b==b and 0.0<=a<=b and 50.0<b<6000.0:
            print('  pos%+d  %.1f / %.1f'%(off,a,b))

k32.CloseHandle(ctypes.c_void_p(h))
