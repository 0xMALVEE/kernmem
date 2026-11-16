import ctypes, ctypes.wintypes as w, struct

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

# exactly what the loader now does: DOS path first, then the object path
h=None
for dev in (r'\\.\Kernmem', r'\\?\GLOBALROOT\Device\Kernmem'):
    hh=k32.CreateFileW(dev,GR|GW,SA,None,OE,0,None)
    print('open %-42s %s'%(dev,'OK' if hh and hh!=INV else 'fail %d'%ctypes.get_last_error()))
    if hh and hh!=INV:
        h=hh
        break
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
print('pid=%d base=0x%X'%(pid,base))

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r else None

def f32(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r else None

def read_pos(obj):
    for off in (0x4584,0x4484):
        x=f32(obj+off); y=f32(obj+off+4); z=f32(obj+off+8)
        if None in (x,y,z): continue
        if x!=x or y!=y or z!=z: continue
        if abs(x)<1.0 or abs(y)<1.0: continue
        if abs(x)>100000 or abs(y)>100000 or abs(z)>10000: continue
        return off,(x,y,z)
    return None,None

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('local=0x%X'%local)

mg=u64(ARR); begin=u64(mg+0); end=u64(mg+8)
print('manager=0x%X begin=0x%X end=0x%X count=%s'%(mg,begin,end,(end-begin)//8 if begin and end and end>begin else 'n/a'))

if not (begin and end and end>begin):
    raise SystemExit('walk would bail')

count=(end-begin)//8
heroes=[]
for i in range(min(count,0x400)):
    obj=u64(begin+i*8)
    if not obj or obj==local: continue
    team=i32(obj+0x128)
    if team not in (1,2): continue
    off,pos=read_pos(obj)
    if not pos: continue
    hp=f32(obj+off+0x40); mx=f32(obj+off+0x44)
    n=rd(obj+0x3F00,32)
    nm=n.decode('utf-16-le','replace').split('\x00')[0] if n else ''
    heroes.append(obj)
    print('  HERO team=%d pos=(%.1f, %.1f, %.1f) hp=%.1f/%.1f name=%r (pos offset +0x%X)'%(team,pos[0],pos[1],pos[2],hp or 0,mx or 0,nm,off))

print('heroes collected: %d'%len(heroes))

rng=f32(local+0x30B0)
print('own attack range: %s'%rng)
print('ring radius in radar px (range/20): %s'%(int(rng/20) if rng else None))

k32.CloseHandle(ctypes.c_void_p(h))
