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

CAM=base+0x1D806C0
OLDBASE=base+0x1D7CC60
for label,addr in (('found',CAM),('sig',OLDBASE)):
    a=rd(addr,0x80); time.sleep(0.6); b=rd(addr,0x80)
    print('%s base+0x%X live=%s crc=%08X'%(label,addr-base,a!=b,sum(b)%0xFFFFFFFF))

raw=rd(CAM,0x80)
V=list(struct.unpack_from('<16f',raw,0))
P=list(struct.unpack_from('<16f',raw,0x40))
print('\nV at found:')
for r in range(4):
    print('   %s'%(' '.join('%10.4f'%V[r*4+c] for c in range(4))))
print('P at found:')
for r in range(4):
    print('   %s'%(' '.join('%10.4f'%P[r*4+c] for c in range(4))))

local=u64(base+0x1CBB810)
pos=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
rng=f32(local+0x3E30) or 700.0
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('\nclient %dx%d pos=%s range=%.1f'%(W,H,pos,rng))

def T(m): return [m[c*4+r] for r in range(4) for c in range(4)]
def mul(A,B): return [sum(A[r*4+k]*B[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]
def appR(M,p):
    x,y,z=p
    return (x*M[0]+y*M[4]+z*M[8]+M[12], x*M[1]+y*M[5]+z*M[9]+M[13], x*M[3]+y*M[7]+z*M[11]+M[15])
def appC(M,p):
    x,y,z=p
    return (x*M[0]+y*M[1]+z*M[2]+M[3], x*M[4]+y*M[5]+z*M[6]+M[7], x*M[12]+y*M[13]+z*M[14]+M[15])

ring=[(pos[0]+rng*math.cos(2*math.pi*i/48), pos[1]+rng*math.sin(2*math.pi*i/48), pos[2]) for i in range(48)]
rows=[]
for vname,Vv in (('V',V),('Vt',T(V))):
    for pname,Pp in (('P',P),('Pt',T(P))):
        for order in ('VP','PV'):
            M=mul(Vv,Pp) if order=='VP' else mul(Pp,Vv)
            for form,app in (('row',appR),('col',appC)):
                cx,cy,cw=app(M,pos)
                if abs(cw)<1e-3: continue
                nx,ny=cx/cw,cy/cw
                if abs(nx)>8 or abs(ny)>8: continue
                sx=CX*(1+nx); sy=CY*(1-ny)
                off=math.hypot(sx-CX,sy-CY)
                pts=[]
                for q in ring:
                    qx,qy,qw=app(M,q)
                    if abs(qw)<1e-3: continue
                    pts.append((CX*(1+qx/qw), CY*(1-qy/qw)))
                if len(pts)<30: continue
                xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
                bw=max(xs)-min(xs); bh=max(ys)-min(ys)
                if bw<5 or bh<5: continue
                circ=abs(bw-bh)/max(bw,bh)
                on=sum(1 for p in pts if 0<=p[0]<W and 0<=p[1]<H)
                rad=(bw+bh)/4
                exp=abs(0.5*W*P[0]*rng/cw)
                score=off*0.5+abs(rad-exp)/max(exp,1)*300+circ*250+(48-on)*5
                rows.append((score,vname,pname,order,form,off,rad,exp,circ,on,sx,sy))
rows.sort()
print('\n score V   P   ord form  off   rad   exp  circ on  screen')
for r in rows[:8]:
    print(' %6.0f %-3s %-3s %s %-4s %6.0f %5.1f %5.1f %.3f %2d (%.0f,%.0f)'%r)
if rows:
    b=rows[0]
    print('\nWINNER %s %s %s %s off=%.0f rad=%.1f exp=%.1f circ=%.3f on=%d'%(
          b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9]))

k32.CloseHandle(ctypes.c_void_p(h))
