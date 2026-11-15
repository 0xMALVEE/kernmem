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
for i in range(25):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        print('camera live after %.1fs'%((i+1)*0.4)); break

raw=rd(CAM,0x80)
V0=list(struct.unpack_from('<16f',raw,0))
P0=list(struct.unpack_from('<16f',raw,0x40))
local=u64(base+0x1CBB810)
pos=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
rng=f32(local+0x3E30) or 700.0
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('client %dx%d pos=%s range=%.1f'%(W,H,pos,rng))

def T(m): return [m[c*4+r] for r in range(4) for c in range(4)]

def inv4(m):
    a=[[m[r*4+c] for c in range(4)]+[1.0 if r==c else 0.0 for c in range(4)] for r in range(4)]
    for col in range(4):
        piv=max(range(col,4),key=lambda r:abs(a[r][col]))
        if abs(a[piv][col])<1e-12: return None
        a[col],a[piv]=a[piv],a[col]
        d=a[col][col]
        a[col]=[v/d for v in a[col]]
        for r in range(4):
            if r!=col and abs(a[r][col])>1e-12:
                f=a[r][col]
                a[r]=[a[r][k]-f*a[col][k] for k in range(8)]
    return [a[r][c+4] for r in range(4) for c in range(4)]

def app(m,p,form):
    x,y,z=p
    if form=='row':
        return (x*m[0]+y*m[4]+z*m[8]+m[12], x*m[1]+y*m[5]+z*m[9]+m[13],
                x*m[2]+y*m[6]+z*m[10]+m[14], x*m[3]+y*m[7]+z*m[11]+m[15])
    return (x*m[0]+y*m[1]+z*m[2]+m[3], x*m[4]+y*m[5]+z*m[6]+m[7],
            x*m[8]+y*m[9]+z*m[10]+m[11], x*m[12]+y*m[13]+z*m[14]+m[15])

views={'V':V0,'Vt':T(V0)}
iv=inv4(V0)
if iv:
    views['Vi']=iv
    views['Vit']=T(iv)
projs={'P':P0,'Pt':T(P0)}

ring=[(pos[0]+rng*math.cos(2*math.pi*i/48), pos[1]+rng*math.sin(2*math.pi*i/48), pos[2]) for i in range(48)]
rows=[]
for vn,V in views.items():
    for pn,P in projs.items():
        for vf in ('row','col'):
            for pf in ('row','col'):
                cx_,cy_,cz_,cw_=app(V,pos,vf)
                clip=app(P,(cx_,cy_,cz_),pf)
                for sgn in (1.0,-1.0):
                    dv=sgn*clip[3]
                    if abs(dv)<1e-6: continue
                    nx=clip[0]/dv; ny=clip[1]/dv
                    if abs(nx)>8 or abs(ny)>8: continue
                    sx=CX*(1+nx); sy=CY*(1-ny)
                    off=math.hypot(sx-CX,sy-CY)
                    pts=[]
                    for q in ring:
                        qx,qy,qz,_=app(V,q,vf)
                        c2=app(P,(qx,qy,qz),pf)
                        d2=sgn*c2[3]
                        if abs(d2)<1e-6: continue
                        pts.append((CX*(1+c2[0]/d2), CY*(1-c2[1]/d2)))
                    if len(pts)<30: continue
                    xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
                    bw=max(xs)-min(xs); bh=max(ys)-min(ys)
                    if bw<3 or bh<3: continue
                    circ=abs(bw-bh)/max(bw,bh)
                    on=sum(1 for p in pts if 0<=p[0]<W and 0<=p[1]<H)
                    rad=(bw+bh)/4
                    score=off*0.6+circ*400+(48-on)*8
                    rows.append((score,vn,pn,vf,pf,sgn,off,rad,circ,on,sx,sy,cz_,clip[3]))
rows.sort()
print('\n score view proj vf  pf  sgn   off   rad  circ on  screen')
for r in rows[:10]:
    print(' %6.0f %-4s %-4s %-3s %-3s %+d %6.0f %5.1f %.3f %2d (%.0f,%.0f)'%r[:12])
if rows:
    b=rows[0]
    print('\nWINNER view=%s proj=%s vform=%s pform=%s wsign=%+d'%(b[1],b[2],b[3],b[4],b[5]))
    print('  offCentre=%.0f radius=%.1f circ=%.3f onScreen=%d screen=(%.0f,%.0f)'%(b[6],b[7],b[8],b[9],b[10],b[11]))
    print('  cam_z=%.1f clip_w=%.1f'%(b[12],b[13]))

k32.CloseHandle(ctypes.c_void_p(h))
