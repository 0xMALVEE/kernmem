import ctypes, ctypes.wintypes as w, struct, math, time

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

def f32(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r and len(r)==4 else None

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

raw=rd(base+0x1D7CC60,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)

CHAINS=(0x5BA8,0x1790,0x2AD0,0x3098)
def hero_pos(obj):
    for co in CHAINS:
        t=u64(obj+co)
        if not t or t<0x10000000000 or t>0x7FFFFFFFFFFF: continue
        if base<=t<base+0x4000000: continue
        a=t+0x2E4
        x,y,z=f32(a),f32(a+4),f32(a+8)
        if None in (x,y,z) or not all(v==v for v in (x,y,z)): continue
        if abs(x)<200.0 or abs(y)<200.0 or abs(x)>20000.0 or abs(y)>20000.0 or abs(z)>3000.0: continue
        return (x,y,z)
    return None

def project(p):
    px,py,pz=p
    cx=px*M[0]+py*M[4]+pz*M[8]+M[12]
    cy=px*M[1]+py*M[5]+pz*M[9]+M[13]
    cw=px*M[3]+py*M[7]+pz*M[11]+M[15]
    if abs(cw)<1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

lp=hero_pos(local)
print('client %dx%d centre (%.0f,%.0f)'%(W,H,CX,CY))
print('local pos=%s'%(lp,))
s=project(lp) if lp else None
print('local screen=%s offCentre=%s on=%s'%('none' if not s else '(%.0f, %.0f)'%s,
      'n/a' if not s else '%.0f'%math.hypot(s[0]-CX,s[1]-CY),
      'n/a' if not s else (0<=s[0]<W and 0<=s[1]<H)))

print('\n-- enemy --')
for i in range(0x400):
    o2=u64(ARR+i*8)
    if not o2 or o2==local: continue
    if i32(o2+0x1638) not in (1,2): continue
    p=hero_pos(o2)
    if not p: continue
    nm=rd(o2+0x3F00,32)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    q=project(p)
    print('  [%d] team=%d %r pos=(%.1f, %.1f, %.1f) dist=%.0f screen=%s'%(
        i,i32(o2+0x1638),nm,p[0],p[1],p[2],math.dist(p,lp),
        'none' if not q else '(%.0f, %.0f)'%q))

if lp:
    rng=f32(local+0x3E30) or 700.0
    pts=[project((lp[0]+rng*math.cos(2*math.pi*i/64), lp[1]+rng*math.sin(2*math.pi*i/64), lp[2])) for i in range(64)]
    pts=[q for q in pts if q]
    if pts:
        xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
        bw=max(xs)-min(xs); bh=max(ys)-min(ys)
        print('\nring %d/64 bbox %.0fx%.0f circ=%.3f on=%d'%(len(pts),bw,bh,
              abs(bw-bh)/max(bw,bh,1),sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)))

    # where does screen centre map back to, on the hero's plane?
    zv=lp[0]*V[2]+lp[1]*V[6]+lp[2]*V[10]+V[14]
    cx=lp[0]*V[0]+lp[1]*V[4]+lp[2]*V[8]+V[12]
    cy=lp[0]*V[1]+lp[1]*V[5]+lp[2]*V[9]+V[13]
    print('\nhero in camera space (%.0f, %.0f, %.0f) dist=%.0f'%(cx,cy,zv,math.sqrt(cx*cx+cy*cy+zv*zv)))
    print('  horizontal angle %.1f deg, vertical %.1f deg'%(
        math.degrees(math.atan2(cx,abs(zv))), math.degrees(math.atan2(cy,abs(zv)))))
    fov=2*math.degrees(math.atan(1.0/P[0]))
    print('  horizontal fov %.1f deg -> half screen = %.1f deg'%(fov,fov/2))

k32.CloseHandle(ctypes.c_void_p(h))
