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
OFF_RANGE=0x17080
OFF_HP=0xED0
OFF_MAXHP=0xED4

h=None
for dev in (r'\\.\Kernmem', r'\\?\GLOBALROOT\Device\Kernmem'):
    hh=k32.CreateFileW(dev,GR|GW,SA,None,OE,0,None)
    if hh and hh!=INV:
        h=hh; break
if not h: raise SystemExit('no device - relaunch the loader')

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

def pos(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r else None

def f1(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r else None

def b1(a):
    r=rd(a,1)
    return r[0] if r else None

def show(t):
    return 'None' if not t else '(%.1f, %.1f, %.1f)'%t

local=u64(base+OFF_LOCAL)
print('local=0x%X'%(local or 0))
if not local: raise SystemExit('local null')

lp=pos(local+OFF_POS)
print('\nlocal: team=%s name=%r pos=%s range=%s hp=%s/%s'%(
    b1(local+OFF_TEAM), asc(local+OFF_NAME), show(lp),
    f1(local+OFF_RANGE), f1(local+OFF_HP), f1(local+OFF_MAXHP)))

lst=u64(base+OFF_LIST)
print('\nlist=0x%X'%(lst or 0))
if not lst: raise SystemExit('list null')

print('\n=== list ===')
n=0
for k in range(24):
    e=u64(lst+k*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF):
        print('  [%d] %s'%(k,'null' if not e else '0x%X'%e)); continue
    t=b1(e+OFF_TEAM)
    p=pos(e+OFF_POS)
    d=math.dist(p,lp) if (p and lp) else -1
    print('  [%d] 0x%X team=%s name=%r pos=%s dist=%.0f hp=%s/%s%s'%(
        k,e,t,asc(e+OFF_NAME),show(p),d,f1(e+OFF_HP),f1(e+OFF_MAXHP),
        ' <== LOCAL' if e==local else ''))
    n+=1
print('entries: %d'%n)

k32.CloseHandle(ctypes.c_void_p(h))
