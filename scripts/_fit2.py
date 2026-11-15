import ctypes, ctypes.wintypes as w, struct, math, time, itertools

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
u32.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u32.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
u32.AttachThreadInput.argtypes=[w.DWORD,w.DWORD,w.BOOL]
u32.SetForegroundWindow.argtypes=[w.HWND]
u32.BringWindowToTop.argtypes=[w.HWND]
u32.ShowWindow.argtypes=[w.HWND,ctypes.c_int]
u32.SwitchToThisWindow.argtypes=[w.HWND,w.BOOL]
u32.IsIconic.argtypes=[w.HWND]
k32.GetCurrentThreadId.restype=w.DWORD
u32.keybd_event.argtypes=[w.BYTE,w.BYTE,w.DWORD,ctypes.c_void_p]

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
M=BASE+0x1D7CC60

for attempt in range(6):
    if u32.GetForegroundWindow()==game:
        break
    if u32.IsIconic(game):
        u32.ShowWindow(game,9); time.sleep(0.4)
    fg=u32.GetForegroundWindow()
    me=k32.GetCurrentThreadId()
    fgp=w.DWORD(); u32.GetWindowThreadProcessId(fg,ctypes.byref(fgp))
    tp=w.DWORD(); u32.GetWindowThreadProcessId(game,ctypes.byref(tp))
    u32.keybd_event(0x12,0,0,None)
    u32.AttachThreadInput(me,tp,True); u32.AttachThreadInput(me,fgp.value,True)
    u32.BringWindowToTop(game)
    u32.SetForegroundWindow(game)
    u32.SwitchToThisWindow(game,True)
    u32.AttachThreadInput(me,tp,False); u32.AttachThreadInput(me,fgp.value,False)
    u32.keybd_event(0x12,0,2,None)
    time.sleep(0.8)
print('focused: %s (fg=0x%X game=0x%X)'%(u32.GetForegroundWindow()==game,u32.GetForegroundWindow(),game))

first=sum(rd(M,0x80))%0xFFFFFFFF
live=False
for i in range(40):
    time.sleep(0.4)
    if sum(rd(M,0x80))%0xFFFFFFFF!=first:
        live=True; print('matrix live after %.1fs'%((i+1)*0.4)); break
print('matrix live: %s'%live)

local=u64(BASE+0x1CBB810)
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
        mate=p; break
    if mate: break

raw=rd(M,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
pos=struct.unpack('<3f',rd(local+0x4584,12))
rng=struct.unpack('<f',rd(local+0x30B0,4))[0]
print('client %dx%d self %s mate %s range %.1f'%(W,H,tuple(round(v,1) for v in pos),
      None if not mate else tuple(round(v,1) for v in mate),rng))
if mate:
    print('self->mate world dist %.0f'%math.dist(pos,mate))

ring=[]
for i in range(64):
    a=2*math.pi*i/64
    ring.append((pos[0]+rng*math.cos(a), pos[1]+rng*math.sin(a), pos[2]))

def evaluate(tf):
    def proj(p):
        q=tf(p)
        vx=q[0]*V[0]+q[1]*V[4]+q[2]*V[8]+V[12]
        vy=q[0]*V[1]+q[1]*V[5]+q[2]*V[9]+V[13]
        vz=q[0]*V[2]+q[1]*V[6]+q[2]*V[10]+V[14]
        cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
        cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
        cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
        if abs(cw)<1e-3: return None
        return (CX*(1+cx/cw), CY*(1-cy/cw))
    s=proj(pos)
    if not s: return None
    off=math.hypot(s[0]-CX, s[1]-CY)
    mp=proj(mate) if mate else s
    mate_px=math.hypot(mp[0]-s[0],mp[1]-s[1]) if mp else 0
    pts=[proj(p) for p in ring]
    pts=[p for p in pts if p]
    if len(pts)<40: return None
    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
    bw=max(xs)-min(xs); bh=max(ys)-min(ys)
    circ=abs(bw-bh)/max(bw,bh,1)
    exp=abs(700.0/(abs(pos[0]*V[2]+pos[1]*V[6]+pos[2]*V[10]+V[14]) or 1)*CX*P[0])
    rad=(bw+bh)/4
    on=sum(1 for p in pts if 0<=p[0]<W and 0<=p[1]<H)
    score=off/10+circ*100+abs(rad-exp)/max(exp,1)+ (64-on)*0.5
    return score,off,circ,rad,exp,on,s,mate_px

rows=[]
for perm in itertools.permutations(range(3)):
    for signs in itertools.product((1,-1),repeat=3):
        def tf(p,perm=perm,signs=signs):
            v=[p[i] for i in perm]
            return tuple(v[i]*signs[i] for i in range(3))
        r=evaluate(tf)
        if not r: continue
        rows.append((r[0],perm,signs,r))

rows.sort()
print('\n  score perm        signs      offCentre circ   rad   exp   on  screen')
for row in rows[:8]:
    r=row[3]
    print('  %6.1f %s %s %7.0f %6.3f %5.1f %5.1f %3d  (%.0f,%.0f)'%(
        row[0],row[1],row[2],r[1],r[2],r[3],r[4],r[5],r[6][0],r[6][1]))

k32.CloseHandle(ctypes.c_void_p(h))
