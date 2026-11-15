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
OFF_NAME_A=0x1E8
OFF_NAME_B=0x3890
OFF_POS=0x25C
OFF_RANGE=0x17080

h=None
for dev in (r'\\.\VanguardBypass', r'\\?\GLOBALROOT\Device\VanguardBypass'):
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

def nm(a):
    r=rd(a,48)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

local=u64(base+OFF_LOCAL)
print('local=0x%X'%(local or 0))
if not local: raise SystemExit('local null')

print('\nlocal checks:')
for label,off in (('team',OFF_TEAM),('nameA',OFF_NAME_A),('nameB',OFF_NAME_B)):
    r=rd(local+off,48 if 'name' in label else 1)
    if not r: 
        print('  %s unreadable'%label); continue
    if 'name' in label:
        print('  %s = %r'%(label, r.decode('utf-16-le','replace').split('\x00')[0]))
    else:
        print('  %s = %d'%(label, r[0]))
p=rd(local+OFF_POS,12)
print('  position = %s'%(struct.unpack('<3f',p) if p else None))
rf=rd(local+OFF_RANGE,4)
print('  range = %s'%(struct.unpack('<f',rf)[0] if rf else None))

lst=u64(base+OFF_LIST)
print('\nlist=0x%X'%(lst or 0))
if not lst: raise SystemExit('list null')

print('\n=== hero list ===')
for k in range(24):
    e=u64(lst+k*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF):
        print('  [%d] %s'%(k,'null' if not e else hex(e)))
        continue
    t=rd(e+OFF_TEAM,1)
    a=nm(e+OFF_NAME_A)
    b=nm(e+OFF_NAME_B)
    pp=rd(e+OFF_POS,12)
    pos=struct.unpack('<3f',pp) if pp else None
    d=math.dist(pos,struct.unpack('<3f',p)) if (pos and p) else 0
    print('  [%d] 0x%X team=%s pos=%s dist=%.0f nameA=%r nameB=%r%s'%(
        k,e,t[0] if t else None,pos,d,a,b,' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
