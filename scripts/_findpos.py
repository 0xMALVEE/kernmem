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
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
print('client %dx%d local=0x%X'%(W,H,local))

cam=tuple(-(V[12]*V[r]+V[13]*V[4+r]+V[14]*V[8+r]) for r in range(3))
print('camera from matrix: (%.0f, %.0f, %.0f)'%cam)

buf=rd(local,0x9000)
print('object bytes: %d'%len(buf))

def project(p):
    px,py,pz=p
    vx=px*V[0]+py*V[4]+pz*V[8]+V[12]
    vy=px*V[1]+py*V[5]+pz*V[9]+V[13]
    vz=px*V[2]+py*V[6]+pz*V[10]+V[14]
    cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
    cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
    cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
    if abs(cw)<1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

rows=[]
for off in range(0, len(buf)-16, 4):
    p=struct.unpack_from('<3f',buf,off)
    if not all(v==v and abs(v)<1e6 for v in p): continue
    if not (500.0 < p[0] < 25000.0 and 500.0 < p[1] < 25000.0): continue
    if not (-2000.0 < p[2] < 3000.0): continue
    s=project(p)
    if not s: continue
    if not (0<=s[0]<W and 0<=s[1]<H): continue
    dist=math.dist(p,cam)
    if dist>6000: continue
    pts=[project((p[0]+700*math.cos(2*math.pi*i/32), p[1]+700*math.sin(2*math.pi*i/32), p[2])) for i in range(32)]
    pts=[q for q in pts if q]
    if len(pts)<28: continue
    xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
    bw=max(xs)-min(xs); bh=max(ys)-min(ys)
    if bw<20 or bh<20: continue
    circ=abs(bw-bh)/max(bw,bh)
    off_c=math.hypot(s[0]-CX, s[1]-CY)
    on=sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)
    score=off_c + circ*400 + (32-on)*10
    rows.append((score, off, p, s, off_c, circ, (bw+bh)/4, on, dist))

rows.sort()
print('\n  score  offset  world                          screen          offCentre circ  radius on  camDist')
for r in rows[:10]:
    print('  %6.0f  +0x%04X (%.1f, %.1f, %.1f) (%6.0f,%6.0f) %8.0f %6.3f %6.1f %2d %7.0f'%(
        r[0],r[1],r[2][0],r[2][1],r[2][2],r[3][0],r[3][1],r[4],r[5],r[6],r[7],r[8]))

k32.CloseHandle(ctypes.c_void_p(h))
