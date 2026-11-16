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
for dev in (r'\\.\Kernmem', r'\\?\GLOBALROOT\Device\Kernmem'):
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
for i in range(30):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        print('camera live after %.1fs'%((i+1)*0.4)); break
else:
    print('camera NEVER live')

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('client %dx%d local=0x%X'%(W,H,local))

print('\n=== enemy identification ===')
enemy=None
for i in range(0x400):
    e=u64(ARR+i*8)
    if not e or e==local: continue
    if i32(e+0x1638) not in (1,2): continue
    enemy=e
    vt=u64(e)
    nm=rd(e+0x3F00,48)
    nm=nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''
    print('  [%d] 0x%X vt=0x%X name=%r team=%s'%(i,e,vt or 0,nm,i32(e+0x1638)))
    print('  local vt=0x%X  same class=%s'%(u64(local) or 0, vt==u64(local)))
    buf=rd(e,0x9000)
    if not buf: continue
    lp=f32((u64(local+0x5BA8) or 0)+0x2E4)
    print('  -- plausible world triples on enemy, 200..15000 from local --')
    found=0
    for off in range(0,len(buf)-12,4):
        p=struct.unpack_from('<3f',buf,off)
        if not all(v==v and abs(v)<1e6 for v in p): continue
        x,y,z=p
        if not (500.0<x<20000.0 and 500.0<y<20000.0 and -500.0<z<3000.0): continue
        q1=struct.unpack_from('<3f',buf,off)
        if q1!=p: continue
        d=math.dist(p,(lp,0,0)[0:1]+(0,0)) if False else 0
        found+=1
        print('     +0x%04X (%.1f, %.1f, %.1f)'%(off,x,y,z))
        if found>=10: break
    if not found: print('     none')

raw=rd(CAM,0x80)
V0=list(struct.unpack_from('<16f',raw,0)); P0=list(struct.unpack_from('<16f',raw,0x40))

posA=(f32(local+0x5BA8) and 0) or 0
ptr=u64(local+0x5BA8)
pos=struct.unpack('<3f',rd(ptr+0x2E4,12))
print('\nlocal pos=%s'%(pos,))

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

def mul(A,B):
    return [sum(A[r*4+k]*B[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

def appR(M,p):
    x,y,z=p
    return (x*M[0]+y*M[4]+z*M[8]+M[12], x*M[1]+y*M[5]+z*M[9]+M[13],
            x*M[2]+y*M[6]+z*M[10]+M[14], x*M[3]+y*M[7]+z*M[11]+M[15])

def appC(M,p):
    x,y,z=p
    return (x*M[0]+y*M[1]+z*M[2]+M[3], x*M[4]+y*M[5]+z*M[6]+M[7],
            x*M[8]+y*M[9]+z*M[10]+M[11], x*M[12]+y*M[13]+z*M[14]+M[15])

variants={'V':V0,'Vt':T(V0),'Pi':P0,'Pt':T(P0)}
inv=inv4(V0)
if inv: variants['Vi']=inv; variants['Vit']=T(inv)

rows=[]
for vname,V in variants.items():
    for pname,P in variants.items():
        if vname[0]==pname[0]: continue
        for form,app in (('row',appR),('col',appC)):
            vx,vy,vz,vw=app(V,pos)
            if abs(vz)<1e-3 and abs(vw)<1e-3: continue
            depth=abs(vz) if abs(vz)>1e-3 else abs(vw)
            cx,cy,cz,cw=app(P,(vx,vy,vz))
            if abs(cw)<1e-6: continue
            nx,ny=cx/cw,cy/cw
            if abs(nx)>6 or abs(ny)>6: continue
            sx=CX*(1+nx); sy=CY*(1-ny)
            off=math.hypot(sx-CX,sy-CY)
            exp=0.5*W*P0[0]*700.0/depth
            pts=[]
            for i in range(48):
                a=2*math.pi*i/48
                q=(pos[0]+700*math.cos(a), pos[1]+700*math.sin(a), pos[2])
                qx,qy,qz,qw=app(V,q)
                rx,ry,rz,rw=app(P,(qx,qy,qz))
                if abs(rw)>1e-6:
                    pts.append((CX*(1+rx/rw), CY*(1-ry/rw)))
            if len(pts)<30: continue
            xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
            bw=max(xs)-min(xs); bh=max(ys)-min(ys)
            if bw<5 or bh<5: continue
            circ=abs(bw-bh)/max(bw,bh)
            on=sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)
            rad=(bw+bh)/4
            score=off*0.4+abs(rad-exp)/max(exp,1)*300+circ*200+(48-on)*4
            rows.append((score,vname,pname,form,off,rad,exp,circ,on,sx,sy))

rows.sort()
print('\n=== convention grid (score, view, proj, form, offCentre, rad, expect, circ, on) ===')
for r in rows[:8]:
    print('  %7.1f %-4s %-4s %-3s off=%7.0f rad=%6.1f exp=%6.1f circ=%.3f on=%2d screen=(%.0f,%.0f)'%r)

k32.CloseHandle(ctypes.c_void_p(h))
