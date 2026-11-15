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
u32.GetForegroundWindow.restype=w.HWND
u32.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
u32.AttachThreadInput.argtypes=[w.DWORD,w.DWORD,w.BOOL]
u32.SetForegroundWindow.argtypes=[w.HWND]
u32.BringWindowToTop.argtypes=[w.HWND]
u32.ShowWindow.argtypes=[w.HWND,ctypes.c_int]
u32.keybd_event.argtypes=[w.BYTE,w.BYTE,w.DWORD,ctypes.c_void_p]
k32.GetCurrentThreadId.restype=w.DWORD

try:
    u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    u32.SetProcessDPIAware()

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

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
for _ in range(6):
    if u32.GetForegroundWindow()==game: break
    fg=u32.GetForegroundWindow(); me=k32.GetCurrentThreadId()
    fgp=w.DWORD(); u32.GetWindowThreadProcessId(fg,ctypes.byref(fgp))
    tp=w.DWORD(); u32.GetWindowThreadProcessId(game,ctypes.byref(tp))
    u32.keybd_event(0x12,0,0,None)
    u32.AttachThreadInput(me,tp,True); u32.AttachThreadInput(me,fgp.value,True)
    u32.ShowWindow(game,9); u32.BringWindowToTop(game); u32.SetForegroundWindow(game)
    u32.AttachThreadInput(me,tp,False); u32.AttachThreadInput(me,fgp.value,False)
    u32.keybd_event(0x12,0,2,None); time.sleep(0.8)
print('focused=%s'%(u32.GetForegroundWindow()==game))

CAM=base+0x1D7CC60
first=sum(rd(CAM,0x80))%0xFFFFFFFF
for i in range(25):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        print('camera live after %.1fs'%((i+1)*0.4)); break

raw=rd(CAM,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2

local=u64(base+0x1CBB810)
pos=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
rng=f32(local+0x3E30) or 700.0
print('client %dx%d local=0x%X pos=%s range=%.1f'%(W,H,local,pos,rng))

def rawproj(p):
    x,y,z=p
    cx=x*M[0]+y*M[4]+z*M[8]+M[12]
    cy=x*M[1]+y*M[5]+z*M[9]+M[13]
    cw=x*M[3]+y*M[7]+z*M[11]+M[15]
    if abs(cw)<1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

s=rawproj(pos)
if not s:
    print('local does not project'); raise SystemExit
ox=CX-s[0]; oy=CY-s[1]
print('raw self=(%.0f, %.0f) correction=(%+.0f, %+.0f)'%(s[0],s[1],ox,oy))

def proj(p):
    q=rawproj(p)
    return None if not q else (q[0]+ox, q[1]+oy)

after=proj(pos)
print('corrected self=(%.0f, %.0f) offCentre=%.1f'%(after[0],after[1],math.hypot(after[0]-CX,after[1]-CY)))

pts=[proj((pos[0]+rng*math.cos(2*math.pi*i/64), pos[1]+rng*math.sin(2*math.pi*i/64), pos[2])) for i in range(64)]
pts=[q for q in pts if q]
if pts:
    xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
    rcx=(min(xs)+max(xs))/2; rcy=(min(ys)+max(ys))/2
    bw=max(xs)-min(xs); bh=max(ys)-min(ys)
    print('ring %d/64 centre=(%.0f, %.0f) centreOffsetFromHero=%.1f bbox %.0fx%.0f on=%d'%(
        len(pts),rcx,rcy,math.hypot(rcx-after[0],rcy-after[1]),bw,bh,
        sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)))

ARR=base+0x1CBB7F8
print('\n-- heroes --')
for i in range(0x400):
    e=u64(ARR+i*8)
    if not e or e==local: continue
    if i32(e+0x1638) not in (1,2): continue
    nm=rd(e+0x3F00,32)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    p=None
    for co in (0x5BA8,0x1790,0x2AD0,0x3098,0x61E4):
        if co==0x61E4:
            t=struct.unpack('<3f',rd(e+0x61E4,12))
        else:
            tp=u64(e+co)
            raw2=rd(tp+0x2E4,12) if tp and tp>0x10000000000 else None
            t=struct.unpack('<3f',raw2) if raw2 else None
        if not t: continue
        x,y,z=t
        if not all(v==v and abs(v)<1e6 for v in t): continue
        if not (200.0<x<20000.0 and 200.0<y<20000.0 and -500.0<z<3000.0): continue
        p=t; break
    if not p: continue
    q=proj(p)
    print('  [%d] team=%d %r pos=(%.1f,%.1f,%.1f) dist=%.0f screen=%s'%(
        i,i32(e+0x1638),nm,p[0],p[1],p[2],math.dist(p,pos),
        'none' if not q else '(%.0f, %.0f)'%q))

k32.CloseHandle(ctypes.c_void_p(h))
