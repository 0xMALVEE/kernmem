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
OFF_LIST2=0x1CB84C8

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

def dump_obj(tag,e):
    print('  %s 0x%X'%(tag,e))
    print('     dump fields : team@0x259=%s dead@0x250=%s vis@0x300=%s targ@0xED0=%s'%(
        b1(e+0x259), b1(e+0x250), b1(e+0x300), b1(e+0xED0)))
    print('     pattern flds: team@0x164=%s name@0x3890=%r name@0x1E8=%r'%(
        b1(e+0x164), asc(e+0x3890), asc(e+0x1E8)))
    print('     oObjName@0x68=%r'%(asc(e+0x68),))
    print('     pos@0x25C=%s hp@0x1080=%s maxhp@0x10A8=%s'%(
        show(pos(e+0x25C)), f1(e+0x1080), f1(e+0x10A8)))

local=u64(base+OFF_LOCAL)
print('\n=== local player 0x%X ==='%(local or 0))
if local: dump_obj('local',local)

for label,glob in (('oHerroList',OFF_LIST),('oHeroList?',OFF_LIST2)):
    lst=u64(base+glob)
    print('\n=== %s base+0x%X -> 0x%X ==='%(label,glob,lst or 0))
    if not lst: continue
    head=rd(lst,0x40)
    if not head:
        print('  unreadable'); continue
    arr=struct.unpack_from('<Q',head,0x8)[0]
    cnt1=struct.unpack_from('<i',head,0x10)[0]
    cnt2=struct.unpack_from('<i',head,0x14)[0]
    print('  array=0x%X count(+0x10)=%d cap(+0x14)=%d'%(arr,cnt1,cnt2))
    if not arr or cnt1<1 or cnt1>64: continue
    for k in range(cnt1):
        e=u64(arr+k*8)
        if not e or not (0x10000000000<e<0x7FFFFFFFFFFF):
            print('  [%d] %s'%(k,'null' if not e else hex(e))); continue
        dump_obj('[%d]'%k,e)

k32.CloseHandle(ctypes.c_void_p(h))
