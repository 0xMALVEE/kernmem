import ctypes, ctypes.wintypes as w, struct, math

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
u32=ctypes.WinDLL('user32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]
u32.FindWindowW.restype=w.HWND
u32.FindWindowW.argtypes=[w.LPCWSTR,w.LPCWSTR]
u32.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]

try:
    u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    try:
        ctypes.WinDLL('shcore').SetProcessDpiAwareness(2)
    except Exception:
        u32.SetProcessDPIAware()

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
    return struct.unpack('<Q',r)[0] if r else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('DPI-aware client %dx%d centre (%.0f,%.0f)'%(W,H,CX,CY))

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('local=0x%X'%local)

SIZE=0x6000
objs={}
for i in range(10):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    b=rd(e,SIZE)
    if not b: continue
    vt=struct.unpack_from('<Q',b,0)[0]
    if not (base <= vt < base+0x3000000):
        print('  skip [%d] 0x%X vt=0x%X (not in module)'%(i,e,vt)); continue
    nm=rd(e+0x3F00,32)
    objs[e]=b
    print('  keep [%d] 0x%X name=%r'%(i,e,nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''))

names=list(objs)
print('module objects: %d'%len(names))

print('\n-- int32 pair {1,2} anywhere in object, across every pair --')
for ai in range(len(names)):
    for bi in range(ai+1,len(names)):
        a=objs[names[ai]]; b=objs[names[bi]]
        hits=0
        for off in range(0,SIZE-4,4):
            av=struct.unpack_from('<i',a,off)[0]; bv=struct.unpack_from('<i',b,off)[0]
            if {av,bv}=={1,2}:
                print('  +0x%04X  0x%X=%d  0x%X=%d'%(off,names[ai],av,names[bi],bv))
                hits+=1
                if hits>6: break

print('\n-- triples plausible on BOTH objects, same offset --')
def plaus(t):
    x,y,z=t
    if not all(v==v and abs(v)<1e7 for v in t): return False
    return (200.0<x<30000.0 and 200.0<y<30000.0 and -4000.0<z<4000.0)

for ai in range(len(names)):
    for bi in range(ai+1,len(names)):
        a=objs[names[ai]]; b=objs[names[bi]]
        hits=0
        for off in range(0,SIZE-12,4):
            pa=struct.unpack_from('<3f',a,off)
            if not plaus(pa): continue
            pb=struct.unpack_from('<3f',b,off)
            if not plaus(pb): continue
            print('  +0x%04X 0x%X=(%.1f, %.1f, %.1f) 0x%X=(%.1f, %.1f, %.1f)'%(
                off,names[ai],pa[0],pa[1],pa[2],names[bi],pb[0],pb[1],pb[2]))
            hits+=1
            if hits>6: break

k32.CloseHandle(ctypes.c_void_p(h))
