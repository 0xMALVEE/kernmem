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
pid=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
if not pid: raise SystemExit('game not running')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

def u8(a):
    r=rd(a,1)
    return r[0] if r else None

def f32(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r else None

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

def nm(a):
    r=rd(a,32)
    if not r: return ''
    return r.split(b'\x00')[0].decode('ascii','replace')

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
for i in range(20):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        print('camera live after %.1fs'%((i+1)*0.4)); break

raw=rd(CAM,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2

def rawproj(p):
    x,y,z=p
    cx=x*M[0]+y*M[4]+z*M[8]+M[12]
    cy=x*M[1]+y*M[5]+z*M[9]+M[13]
    cw=x*M[3]+y*M[7]+z*M[11]+M[15]
    if abs(cw)<1e-3: return None
    return (cx/cw, cy/cw)

local=u64(base+0x1CFA7B0)
if not local: raise SystemExit('local null')
lpos=f3(local+0xA0)
rng=f32(local+0x17080)
if not rng or not (100.0<rng<1500.0): rng=f32(local+0x3E30)
print('client %dx%d local=0x%X name=%r team=%s pos=%s range=%s'%(
    W,H,local,nm(local+0x3890),u8(local+0x164),lpos,rng))

# replicate CentreOnLocal exactly
step=1000.0
s0=rawproj(lpos); sx1=rawproj((lpos[0]+step,lpos[1],lpos[2])); sy1=rawproj((lpos[0],lpos[1]+step,lpos[2]))
gx=abs((sx1[0]-s0[0])*CX); gy=abs((sy1[1]-s0[1])*CY)
target=(gx+gy)/2.0
scx=min(20.0,max(0.05,target/gx)); scy=min(20.0,max(0.05,target/gy))
def scr(p):
    q=rawproj(p)
    return None if not q else (CX+CX*q[0]*scx, CY-CY*q[1]*scy)
a=scr(lpos); ox=CX-a[0]; oy=CY-a[1]
def proj(p):
    q=scr(p)
    return None if not q else (q[0]+ox,q[1]+oy)
print('scale=(%.2f, %.2f) correction=(%+.0f, %+.0f)'%(scx,scy,ox,oy))

fin=proj(lpos)
print('self=(%.0f, %.0f) offCentre=%.1f'%(fin[0],fin[1],math.hypot(fin[0]-CX,fin[1]-CY)))
if rng:
    pts=[proj((lpos[0]+rng*math.cos(2*math.pi*i/64), lpos[1]+rng*math.sin(2*math.pi*i/64), lpos[2])) for i in range(64)]
    pts=[q for q in pts if q]
    if pts:
        xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
        bw=max(xs)-min(xs); bh=max(ys)-min(ys)
        print('ring %d/64 bbox %.0fx%.0f circ=%.3f radius=%.1f on=%d'%(
            len(pts),bw,bh,abs(bw-bh)/max(bw,bh,1),(bw+bh)/4,
            sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)))

lst=u64(base+0x1CB8550)
arr=u64(lst+8) if lst else None
cnt=i32(lst+0x10) if lst else 0
print('\nhero list: count=%s'%cnt)
for k in range(min(cnt or 0,12)):
    e=u64(arr+k*8)
    if not e: continue
    t=u8(e+0x164)
    p=f3(e+0xA0)
    q=proj(p) if p else None
    d=math.dist(p,lpos) if (p and lpos) else -1
    print('  [%d] %-10r team=%s pos=%s hp=%s/%s dist=%.0f screen=%s%s'%(
        k,nm(e+0x3890),t,p,f32(e+0xA0-4),f32(e+0xA0-8),d,
        'none' if not q else '(%.0f, %.0f)'%q,' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
