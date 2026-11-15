import ctypes, ctypes.wintypes as w, struct, math, time, itertools
import numpy as np

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
for _ in range(5):
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
    time.sleep(0.7)
print('focused=%s'%(u32.GetForegroundWindow()==game))

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('client %dx%d centre (%.0f,%.0f)'%(W,H,CX,CY))

local=u64(BASE+0x1CBB810)
raw=rd(BASE+0x1D7CC60,0x80)
V=np.array(struct.unpack('<16f',raw,0),dtype=np.float64).reshape(4,4)
P=np.array(struct.unpack('<16f',raw,0x40),dtype=np.float64).reshape(4,4)
buf=rd(local,0x9000)
print('object bytes %d  V[3,3]=%.2f P[3,3]=%.2f P[2,3]=%.2f'%(len(buf),V[3,3],P[3,3],P[2,3]))

cand=[]
offs=[]
for off in range(0,len(buf)-12,4):
    x,y,z=struct.unpack_from('<3f',buf,off)
    if not all(v==v and abs(v)<1e6 for v in (x,y,z)): continue
    if not (200.0<x<30000.0 and 200.0<y<30000.0): continue
    if not (-3000.0<z<4000.0): continue
    offs.append(off); cand.append((x,y,z))
print('candidate triples: %d'%len(cand))
if not cand: raise SystemExit('none')
A=np.array(cand,dtype=np.float64)

def project(Pmat, pts):
    ones=np.ones((pts.shape[0],1))
    q=np.hstack([pts,ones])
    view=q@V.T
    clip=view@Pmat.T
    cw=clip[:,3]
    good=np.abs(cw)>1e-3
    sx=np.full(pts.shape[0],np.nan); sy=np.full(pts.shape[0],np.nan)
    sx[good]=CX*(1+clip[good,0]/cw[good])
    sy[good]=CY*(1-clip[good,1]/cw[good])
    return sx,sy

angles=np.array([2*math.pi*i/32 for i in range(32)])
rows=[]
for perm in itertools.permutations(range(3)):
    for signs in itertools.product((1,-1),repeat=3):
        T=A[:,list(perm)]*np.array(signs,dtype=np.float64)
        sx,sy=project(P,T)
        ok=np.isfinite(sx)&np.isfinite(sy)
        offc=np.hypot(sx-CX,sy-CY)
        near=ok&(offc<700)&(sx>-100)&(sx<W+100)&(sy>-100)&(sy<H+100)
        if not near.any(): continue
        idx=np.nonzero(near)[0]
        for i in idx:
            p=T[i]
            ring=np.stack([p[0]+700*np.cos(angles), p[1]+700*np.sin(angles), np.full(32,p[2])],axis=1)
            rx,ry=project(P,ring)
            m=np.isfinite(rx)&np.isfinite(ry)
            if m.sum()<28: continue
            bw=rx[m].max()-rx[m].min(); bh=ry[m].max()-ry[m].min()
            if bw<15 or bh<15: continue
            circ=abs(bw-bh)/max(bw,bh)
            on=int(((rx>-50)&(rx<W+50)&(ry>-50)&(ry<H+50)).sum())
            score=offc[i]+circ*500+(32-on)*8
            rows.append((score,offs[i],perm,signs,offc[i],circ,(bw+bh)/4,on,sx[i],sy[i]))

rows.sort()
print('\n  score offset  perm        signs       offCentre circ  rad   on  screen')
for r in rows[:10]:
    print('  %6.0f +0x%04X %s %s %8.0f %6.3f %5.1f %2d (%6.0f,%6.0f)'%(
        r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],r[8],r[9]))

k32.CloseHandle(ctypes.c_void_p(h))
