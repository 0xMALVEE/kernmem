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
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('client %dx%d centre (%.0f,%.0f)'%(W,H,CX,CY))

raw=rd(base+0x1D7CC60,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

def project(p):
    px,py,pz=p
    cx=px*M[0]+py*M[4]+pz*M[8]+M[12]
    cy=px*M[1]+py*M[5]+pz*M[9]+M[13]
    cw=px*M[3]+py*M[7]+pz*M[11]+M[15]
    if abs(cw)<1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('local=0x%X'%local)

print('\n-- chain +0x5BA8 -> +0x2E4 on every module-vtable object --')
for i in range(10):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e)
    if not vt or not (base <= vt < base+0x3000000): continue
    nm=rd(e+0x3F00,32)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    ptr=u64(e+0x5BA8)
    chained=f3(ptr+0x2E4) if ptr else None
    inline=f3(e+0x4584)
    team=i32(e+0x1638)
    s=project(chained) if chained else None
    print('  [%d] 0x%X %-12r team=%s ptr=0x%X'%(i,e,nm,team,ptr or 0))
    print('      chain=%s inline0x4584=%s'%(chained,inline))
    if s:
        print('      chain screen=(%.0f, %.0f) offCentre=%.0f%s'%(s[0],s[1],
              math.hypot(s[0]-CX,s[1]-CY),' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
