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
CHAIN=0x200
SUBPOS=0x28

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

def pos(obj):
    p=u64(obj+CHAIN)
    if not p or p < 0x10000000000: return None,None
    a=p+SUBPOS
    r=rd(a,12)
    return (struct.unpack('<3f',r) if r else None), a

lst=u64(base+OFF_LIST)
arr=struct.unpack('<Q',rd(lst+8,8))[0]
cnt=struct.unpack('<i',rd(lst+0x10,4))[0]
local=u64(base+OFF_LOCAL)
print('count=%d local=0x%X'%(cnt,local or 0))

heroes=[]
for k in range(min(cnt,12)):
    e=u64(arr+k*8)
    if e and 0x10000000000<e<0x7FFFFFFFFFFF:
        heroes.append((k,e))

print('\n-- stability: same triple on two reads 1.5s apart --')
for k,e in heroes:
    p1,a=pos(e)
    time.sleep(0.15)
    p2,_=pos(e)
    stable = p1 is not None and p1==p2
    print('  [%d] %-10r team=%s stable=%s %s'%(k,asc(e+OFF_NAME),b1(e+OFF_TEAM),stable,p1))

print('\n-- health pair candidates near position (local hero) --')
lp,la=pos(local)
print('local pos=%s addr=0x%X'%(lp,la or 0))
if la:
    for off in range(-0x40, 0x60, 4):
        r=rd(la+off,8)
        if not r: continue
        a,b=struct.unpack('<2f',r)
        if a==a and b==b and 0.0<=a<=b and 20.0<b<8000.0:
            print('  pos%+d hp=%.1f max=%.1f'%(off,a,b))

print('\n-- dump health offsets on local --')
for off in (0x1080,0x10A8,0xED0,0xED4,0x17080):
    r=rd(local+off,4)
    print('  +0x%X = %s'%(off, struct.unpack('<f',r)[0] if r else None))

k32.CloseHandle(ctypes.c_void_p(h))
