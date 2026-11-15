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
u32.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u32.GetWindowThreadProcessId.argtypes=[w.HWND,ctypes.POINTER(w.DWORD)]
u32.AttachThreadInput.argtypes=[w.DWORD,w.DWORD,w.BOOL]
u32.SetForegroundWindow.argtypes=[w.HWND]
u32.BringWindowToTop.argtypes=[w.HWND]
u32.ShowWindow.argtypes=[w.HWND,ctypes.c_int]
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
_,o=ioctl(0x80002024,struct.pack('<II',PID,0),8)
BASE=struct.unpack('<Q',o)[0]

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',PID,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
fg=u32.GetForegroundWindow()
if fg!=game:
    me=k32.GetCurrentThreadId()
    fgp=w.DWORD(); u32.GetWindowThreadProcessId(fg,ctypes.byref(fgp))
    tp=w.DWORD(); u32.GetWindowThreadProcessId(game,ctypes.byref(tp))
    u32.AttachThreadInput(me,tp,True); u32.AttachThreadInput(me,fgp.value,True)
    u32.ShowWindow(game,9); u32.BringWindowToTop(game); u32.SetForegroundWindow(game)
    u32.AttachThreadInput(me,tp,False); u32.AttachThreadInput(me,fgp.value,False)
    time.sleep(1.2)
print('focused: %s'%(u32.GetForegroundWindow()==game))

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
local=u64(BASE+0x1CBB810)
M=BASE+0x1D7CC60

print('\n-- 8 samples: self ndc vs camera/pos change --')
pv=pp=None
for i in range(8):
    raw=rd(M,0x80)
    V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
    pos=struct.unpack('<3f',rd(local+0x4584,12))
    vx=pos[0]*V[0]+pos[1]*V[4]+pos[2]*V[8]+V[12]
    vy=pos[0]*V[1]+pos[1]*V[5]+pos[2]*V[9]+V[13]
    vz=pos[0]*V[2]+pos[1]*V[6]+pos[2]*V[10]+V[14]
    cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
    cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
    cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
    ndc=(cx/cw, cy/cw) if abs(cw)>1e-3 else (None,None)
    vc=sum(V)%0xFFFFF; pc=sum(P)%0xFFFFF
    print('  %d ndc=(%s, %s) viewChanged=%s projChanged=%s m5=%.3f'%(
        i, None if ndc[0] is None else round(ndc[0],3), None if ndc[1] is None else round(ndc[1],3),
        pv is not None and vc!=pv, pp is not None and pc!=pp, P[5]))
    pv, pp = vc, pc
    time.sleep(0.8)

print('\n-- whole-image scan for camera pairs that centre the hero --')
hdr=rd(BASE,0x400)
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]
pos=struct.unpack('<3f',rd(local+0x4584,12))
rows=[]
CH=0x40000
for off in range(0,SIZE,CH):
    buf=rd(BASE+off,min(CH,SIZE-off))
    if not buf: continue
    for i in range(0,len(buf)-0x80,0x10):
        if abs(struct.unpack_from('<f',buf,i+0x3c)[0]-1.0)>0.02: continue
        V=struct.unpack_from('<16f',buf,i)
        if not all(v==v and abs(v)<1e6 for v in V): continue
        ok=True
        for r in range(3):
            n=math.sqrt(V[r*4]**2+V[r*4+1]**2+V[r*4+2]**2)
            if abs(n-1.0)>0.03: ok=False; break
        if not ok: continue
        P=struct.unpack_from('<16f',buf,i+0x40)
        if abs(P[15])>0.03 or abs(P[11]-1.0)>0.03 or P[0]<0.2 or P[5]<0.2: continue
        vx=pos[0]*V[0]+pos[1]*V[4]+pos[2]*V[8]+V[12]
        vy=pos[0]*V[1]+pos[1]*V[5]+pos[2]*V[9]+V[13]
        vz=pos[0]*V[2]+pos[1]*V[6]+pos[2]*V[10]+V[14]
        cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
        if abs(cw)<1e-3: continue
        cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
        cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
        rows.append((math.hypot(cx/cw, cy/cw), BASE+off+i, cx/cw, cy/cw, cw, P[5]))

rows.sort()
print('  pairs found: %d'%len(rows))
for r in rows[:6]:
    print('   off=%.3f addr=0x%X (base+0x%X) ndc=(%.3f, %.3f) w=%.0f m5=%.3f'%(
        r[0], r[1], r[1]-BASE, r[2], r[3], r[4], r[5]))

k32.CloseHandle(ctypes.c_void_p(h))
