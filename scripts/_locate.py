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
PID=struct.unpack('<Q',o)[0]
if not PID: raise SystemExit('game not running')
_,o=ioctl(0x80002024,struct.pack('<II',PID,0),8)
BASE=struct.unpack('<Q',o)[0]

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',PID,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2

local=u64(BASE+0x1CBB810)
raw=rd(BASE+0x1D7CC60,0x80)
V=list(struct.unpack_from('<16f',raw,0))
P=list(struct.unpack_from('<16f',raw,0x40))

# old project's exact product: row-major V*P
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

buf=rd(local,0x9000)
print('client %dx%d local=0x%X bytes=%d'%(W,H,local,len(buf)))

def project(p, gate):
    px,py,pz=p
    cx=px*M[0]+py*M[4]+pz*M[8]+M[12]
    cy=px*M[1]+py*M[5]+pz*M[9]+M[13]
    cw=px*M[3]+py*M[7]+pz*M[11]+M[15]
    if gate and cw < 0.1: return None
    if not gate and abs(cw) < 1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw), cw)

for gate in (True, False):
    tag='w>0.1' if gate else '|w|'
    found=0
    best=[]
    for off in range(0,len(buf)-16,4):
        p=struct.unpack_from('<3f',buf,off)
        if not all(v==v and abs(v)<1e7 for v in p): continue
        if not (100.0<p[0]<30000.0 and 100.0<p[1]<30000.0 and -4000.0<p[2]<4000.0): continue
        s=project(p,gate)
        if not s: continue
        found+=1
        offc=math.hypot(s[0]-CX,s[1]-CY)
        if offc<800: best.append((offc,off,p,s))
    best.sort()
    print('\n[%s] plausible triples: %d, within 800px: %d'%(tag,found,len(best)))
    for offc,off,p,s in best[:5]:
        print('   +0x%X (%.1f, %.1f, %.1f) -> (%.0f, %.0f) w=%.0f off=%.0f'%(
            off,p[0],p[1],p[2],s[0],s[1],s[2],offc))

k32.CloseHandle(ctypes.c_void_p(h))
