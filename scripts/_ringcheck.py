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
u32.GetForegroundWindow.restype=w.HWND
u32.SetForegroundWindow.argtypes=[w.HWND]
u32.BringWindowToTop.argtypes=[w.HWND]
u32.ShowWindow.argtypes=[w.HWND,ctypes.c_int]
u32.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u32.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
u32.AttachThreadInput.argtypes=[w.DWORD,w.DWORD,w.BOOL]
k32.GetCurrentThreadId.restype=w.DWORD

h=None
for dev in (r'\\.\VanguardBypass', r'\\?\GLOBALROOT\Device\VanguardBypass'):
    hh=k32.CreateFileW(dev,GR|GW,SA,None,OE,0,None)
    if hh and hh!=INV:
        h=hh; print('device via',dev); break
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

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
fg=u32.GetForegroundWindow()
print('game=0x%X fg=0x%X focused=%s'%(game,fg,fg==game))

# who owns the foreground window?
fgpid=w.DWORD(); u32.GetWindowThreadProcessId(fg,ctypes.byref(fgpid))
try:
    import subprocess
    nm=subprocess.run(['tasklist','/FI','PID eq %d'%fgpid.value,'/FO','CSV','/NH'],capture_output=True,text=True).stdout.strip()
    print('foreground owner:', nm.split(',')[0] if nm else '?')
except Exception as e:
    print('owner lookup failed', e)

# focus the game
me=k32.GetCurrentThreadId()
tgt=w.DWORD(); u32.GetWindowThreadProcessId(game,ctypes.byref(tgt))
u32.AttachThreadInput(me,tgt,True)
u32.AttachThreadInput(me,fgpid.value,True)
u32.ShowWindow(game,9)
u32.BringWindowToTop(game)
u32.SetForegroundWindow(game)
u32.AttachThreadInput(me,tgt,False)
u32.AttachThreadInput(me,fgpid.value,False)
time.sleep(1.5)
print('focused after: %s'%(u32.GetForegroundWindow()==game))

local=u64(BASE+0x1CBB810)
print('local=0x%X'%local)

print('\n-- near/far position, 3 samples --')
for i in range(3):
    a=rd(local+0x4584,12); b=rd(local+0x4484,12)
    print('  near '+str(struct.unpack('<3f',a) if a else None))
    print('  far  '+str(struct.unpack('<3f',b) if b else None))
    time.sleep(0.6)

print('\n-- camera liveness, 4 samples --')
prev=None
for i in range(4):
    m=rd(BASE+0x1D7CC60,0x80)
    c=sum(m)%0xFFFFFFFF
    print('  crc=%08X changed=%s'%(c, prev is not None and c!=prev))
    prev=c
    time.sleep(0.5)

ARR=BASE+0x1CBB7F8
mate=None
for i in range(8):
    o2=u64(ARR+i*8)
    if not o2 or o2==local: continue
    if i32(o2+0x128) not in (1,2): continue
    for off in (0x4584,0x4484):
        r=rd(o2+off,12)
        if not r: continue
        p=struct.unpack('<3f',r)
        if abs(p[0])<1.0 or abs(p[1])<1.0: continue
        mate=(i,o2,p); break
    if mate: break

raw=rd(BASE+0x1D7CC60,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
sx_,sy_,sz_=struct.unpack('<3f',rd(local+0x4584,12))
print('\nclient %dx%d self (%.1f,%.1f,%.1f)'%(W,H,sx_,sy_,sz_))
if mate:
    print('enemy[%d] %s worldDist %.0f'%(mate[0],tuple(round(v,1) for v in mate[2]),math.dist(mate[2],(sx_,sy_,sz_))))

def project(p):
    px,py,pz=p
    vx=px*V[0]+py*V[4]+pz*V[8]+V[12]
    vy=px*V[1]+py*V[5]+pz*V[9]+V[13]
    vz=px*V[2]+py*V[6]+pz*V[10]+V[14]
    cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
    cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
    cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
    if abs(cw)<=0.001: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

s=project((sx_,sy_,sz_))
print('self -> '+('none' if not s else '(%.0f, %.0f)'%s))
if s:
    print('  offCentre %.0f onscreen %s'%(math.hypot(s[0]-CX,s[1]-CY),0<=s[0]<W and 0<=s[1]<H))
    rng=struct.unpack('<f',rd(local+0x30B0,4))[0]
    print('  own range %.1f'%rng)
    pts=[]
    for i in range(64):
        a=2*math.pi*i/64
        q=project((sx_+rng*math.cos(a), sy_+rng*math.sin(a), sz_))
        if q: pts.append(q)
    if pts:
        xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
        rcx=(min(xs)+max(xs))/2; rcy=(min(ys)+max(ys))/2
        print('  ring %d/64 bbox %.0fx%.0f centre (%.0f,%.0f) centreOffsetFromSelf %.0f'%(
            len(pts),max(xs)-min(xs),max(ys)-min(ys),rcx,rcy,math.hypot(rcx-s[0],rcy-s[1])))
if mate:
    q=project(mate[2])
    print('enemy -> '+('none' if not q else '(%.0f, %.0f)'%q))

k32.CloseHandle(ctypes.c_void_p(h))
