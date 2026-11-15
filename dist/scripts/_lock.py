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
for attempt in range(6):
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
live=False
for i in range(40):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        live=True; print('camera live after %.1fs'%((i+1)*0.4)); break
print('camera live=%s'%live)

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

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
pos=f3((u64(local+0x5BA8) or 0)+0x2E4)
print('client %dx%d local=0x%X pos=%s'%(W,H,local,pos))
s=project(pos) if pos else None
print('local chain -> %s offCentre=%s'%('none' if not s else '(%.0f, %.0f)'%s,
      'n/a' if not s else '%.0f'%math.hypot(s[0]-CX,s[1]-CY)))
if s and pos:
    pts=[project((pos[0]+700*math.cos(2*math.pi*i/64), pos[1]+700*math.sin(2*math.pi*i/64), pos[2])) for i in range(64)]
    pts=[q for q in pts if q]
    if pts:
        xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
        print('ring %d/64 bbox %.0fx%.0f on=%d'%(len(pts),max(xs)-min(xs),max(ys)-min(ys),
              sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)))

print('\n-- other hero: search its own pointer+sub chain --')
for i in range(10):
    e=u64(ARR+i*8)
    if not e or e==local: continue
    if i32(e+0x1638) not in (1,2): continue
    nm=rd(e+0x3F00,32)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    buf=rd(e,0x6000)
    if not buf: continue
    print('  [%d] 0x%X %r team=%s'%(i,e,nm,i32(e+0x1638)))
    for off in range(0,len(buf)-8,8):
        ptr=struct.unpack_from('<Q',buf,off)[0]
        if not (0x10000000000<ptr<0x7FFFFFFFFFFF) or base<=ptr<base+0x3000000: continue
        for sub in (0x2E4,0x2E0,0x264,0x100):
            q=f3(ptr+sub)
            if not q: continue
            if not (200.0<q[0]<20000.0 and 200.0<q[1]<20000.0 and -500.0<q[2]<3000.0): continue
            d=math.dist(q,pos) if pos else 0
            if d<1.0 or d>20000.0: continue
            print('     +0x%04X -> +0x%X (%.1f, %.1f, %.1f) distFromLocal=%.1f'%(off,sub,q[0],q[1],q[2],d))

k32.CloseHandle(ctypes.c_void_p(h))
