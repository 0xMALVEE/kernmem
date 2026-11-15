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

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
local=u64(base+0x1CBB810)
ptr=u64(local+0x5BA8)
chainpos=struct.unpack('<3f',rd(ptr+0x2E4,12))
print('client %dx%d chainpos=%s'%(W,H,chainpos))

buf=rd(local,0x9000)
tbuf=rd(ptr,0x1000) if ptr else None
cands=[]
for src,label in ((buf,'obj'),(tbuf,'chain')):
    if not src: continue
    for off in range(0,len(src)-12,4):
        p=struct.unpack_from('<3f',src,off)
        if not all(v==v and abs(v)<1e6 for v in p): continue
        if not (200.0<p[0]<20000.0 and 200.0<p[1]<20000.0 and -500.0<p[2]<3000.0): continue
        cands.append((label,off,p))
print('candidates %d'%len(cands))

for cname,caddr in (('A',base+0x1D7CC60),('B',base+0x1D806C0)):
    a=rd(caddr,0x80); time.sleep(0.5); b=rd(caddr,0x80)
    if not a or not b:
        print('%s unreadable'%cname); continue
    V=list(struct.unpack_from('<16f',b,0)); P=list(struct.unpack_from('<16f',b,0x40))
    cam=tuple(-(V[12]*V[r]+V[13]*V[4+r]+V[14]*V[8+r]) for r in range(3))
    dv=(chainpos[0]-cam[0],chainpos[1]-cam[1],chainpos[2]-cam[2])
    nd=math.sqrt(sum(v*v for v in dv)) or 1.0
    fwd=(-V[8],-V[9],-V[10])
    dot=sum(fwd[k]*dv[k] for k in range(3))/nd
    print('\n%s live=%s cam=(%.0f,%.0f,%.0f) dist=%.0f angleToChain=%.1f'%(
        cname, a!=b, cam[0],cam[1],cam[2], nd, math.degrees(math.acos(max(-1,min(1,dot))))))
    for form in ('row','col'):
        rows=[]
        for label,off,p in cands:
            x,y,z=p
            if form=='row':
                cx=x*V[0]+y*V[4]+z*V[8]+V[12]
                cy=x*V[1]+y*V[5]+z*V[9]+V[13]
                cw=x*V[3]+y*V[7]+z*V[11]+V[15]
                px=cx*P[0]+cy*P[4]+P[12]
                py=cx*P[1]+cy*P[5]+P[13]
                pw=cx*P[3]+cy*P[7]+P[15]
            else:
                cx=x*V[0]+y*V[1]+z*V[2]+V[3]
                cy=x*V[4]+y*V[5]+z*V[6]+V[7]
                cw=x*V[12]+y*V[13]+z*V[14]+V[15]
                px=cx*P[0]+cy*P[1]+P[3]
                py=cx*P[4]+cy*P[5]+P[7]
                pw=cx*P[12]+cy*P[13]+P[15]
            if abs(pw)<1e-9: continue
            nx=px/pw; ny=py/pw
            if abs(nx)>1.5 or abs(ny)>1.5: continue
            rows.append((math.hypot(nx*CX,ny*CY),label,off,p,nx,ny))
        rows.sort()
        print('  form=%s best:'%form)
        for r in rows[:5]:
            print('    off=%6.0f %s+0x%04X (%.1f,%.1f,%.1f) ndc=(%.2f,%.2f)'%(
                r[0],r[1],r[2],r[3][0],r[3][1],r[3][2],r[4],r[5]))
        if not rows: print('    none within 1.5 ndc')

k32.CloseHandle(ctypes.c_void_p(h))
