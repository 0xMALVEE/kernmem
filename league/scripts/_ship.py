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

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
for _ in range(6):
    if u32.GetForegroundWindow()==game: break
    fg=u32.GetForegroundWindow()
    me=k32.GetCurrentThreadId()
    fgp=w.DWORD(); u32.GetWindowThreadProcessId(fg,ctypes.byref(fgp))
    tp=w.DWORD(); u32.GetWindowThreadProcessId(game,ctypes.byref(tp))
    u32.keybd_event(0x12,0,0,None)
    u32.AttachThreadInput(me,tp,True); u32.AttachThreadInput(me,fgp.value,True)
    u32.ShowWindow(game,9); u32.BringWindowToTop(game); u32.SetForegroundWindow(game)
    u32.AttachThreadInput(me,tp,False); u32.AttachThreadInput(me,fgp.value,False)
    u32.keybd_event(0x12,0,2,None)
    time.sleep(0.8)
print('focused=%s'%(u32.GetForegroundWindow()==game))

CAM=base+0x1D7CC60
first=sum(rd(CAM,0x80))%0xFFFFFFFF
for i in range(40):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        print('camera live after %.1fs'%((i+1)*0.4)); break
else:
    print('camera NEVER live')

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
raw=rd(CAM,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

def project(p):
    px,py,pz=p
    cx=px*M[0]+py*M[4]+pz*M[8]+M[12]
    cy=px*M[1]+py*M[5]+pz*M[9]+M[13]
    cw=px*M[3]+py*M[7]+pz*M[11]+M[15]
    if abs(cw)<1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

CHAINS=(0x5BA8,0x1790,0x2AD0,0x3098)

def hero_pos(obj):
    for co in CHAINS:
        t=u64(obj+co)
        if not t or t<0x10000000000 or t>0x7FFFFFFFFFFF: continue
        if base<=t<base+0x4000000: continue
        a=t+0x2E4
        x,y,z=f32(a),f32(a+4),f32(a+8)
        if None in (x,y,z): continue
        if not all(v==v for v in (x,y,z)): continue
        if abs(x)<200.0 or abs(y)<200.0: continue
        if abs(x)>20000.0 or abs(y)>20000.0 or abs(z)>3000.0: continue
        return (x,y,z),a
    return None,None

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('client %dx%d local=0x%X'%(W,H,local))

lp,la=hero_pos(local)
print('local pos=%s'%(lp,))
if lp:
    s=project(lp)
    print('local screen=%s offCentre=%s'%('none' if not s else '(%.0f, %.0f)'%s,
          'n/a' if not s else '%.0f'%math.hypot(s[0]-CX,s[1]-CY)))

rng=None
for off in (0x3E30,0x30B0):
    v=f32(local+off)
    if v and 100.0<v<1500.0:
        rng=v; print('range from +0x%X = %.1f'%(off,v)); break

print('\n-- collected heroes (new logic) --')
count=0
for i in range(0x400):
    obj=u64(ARR+i*8)
    if not obj or obj==local: continue
    team=i32(obj+0x1638)
    if team not in (1,2): continue
    p,addr=hero_pos(obj)
    if not p: continue
    nm=rd(obj+0x3F00,32)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    hp=f32(addr+0x98); mx=f32(addr+0x9C)
    q=project(p)
    count+=1
    print('  [%d] 0x%X team=%d %r pos=(%.1f, %.1f, %.1f) hp=%s/%s screen=%s'%(
        i,obj,team,nm,p[0],p[1],p[2],hp,mx,'none' if not q else '(%.0f, %.0f)'%q))
print('heroes collected: %d'%count)

if lp and rng:
    pts=[project((lp[0]+rng*math.cos(2*math.pi*i/64), lp[1]+rng*math.sin(2*math.pi*i/64), lp[2])) for i in range(64)]
    pts=[q for q in pts if q]
    if pts:
        xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
        print('ring %d/64 bbox %.0fx%.0f on=%d'%(len(pts),max(xs)-min(xs),max(ys)-min(ys),
              sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)))

k32.CloseHandle(ctypes.c_void_p(h))
