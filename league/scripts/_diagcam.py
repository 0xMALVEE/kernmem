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
for i in range(30):
    time.sleep(0.4)
    if sum(rd(CAM,0x80))%0xFFFFFFFF!=first:
        print('camera live after %.1fs'%((i+1)*0.4)); break
else:
    print('camera NEVER live')

raw=rd(CAM,0x80)
V=list(struct.unpack_from('<16f',raw,0))
P=list(struct.unpack_from('<16f',raw,0x40))
local=u64(base+0x1CBB810)
pos=struct.unpack('<3f',rd((u64(local+0x5BA8) or 0)+0x2E4,12))
print('\nV raw:')
for r in range(4):
    print('   %s'%(' '.join('%10.4f'%V[r*4+c] for c in range(4))))
print('P raw:')
for r in range(4):
    print('   %s'%(' '.join('%10.4f'%P[r*4+c] for c in range(4))))
print('\npos=%s'%(pos,))

# V is orthonormal with translation in the last row; its inverse is the transpose
# of the rotation with a recomputed translation.
Rt=[V[0],V[4],V[8], V[1],V[5],V[9], V[2],V[6],V[10]]
t=(V[12],V[13],V[14])
trans=tuple(-(t[0]*Rt[r*3]+t[1]*Rt[r*3+1]+t[2]*Rt[r*3+2]) for r in range(3))
print('\nif V is camera->world: camera position = %s'%(tuple(round(v,1) for v in trans),))
print('  distance camera->hero = %.1f'%math.dist(trans,pos))

print('\nhero in camera space (world->camera, column form with the inverse):')
x,y,z=pos
cx=x*Rt[0]+y*Rt[3]+z*Rt[6]+trans[0]
cy=x*Rt[1]+y*Rt[4]+z*Rt[7]+trans[1]
cz=x*Rt[2]+y*Rt[5]+z*Rt[8]+trans[2]
print('  (%.1f, %.1f, %.1f)  depth |z|=%.1f'%(cx,cy,cz,abs(cz)))
print('  horizontal angle %.1f deg, vertical %.1f deg'%(
      math.degrees(math.atan2(cx,abs(cz))), math.degrees(math.atan2(cy,abs(cz)))))
print('  vertical half-fov from P: %.1f deg'%math.degrees(math.atan(1.0/P[5])))

print('\ncamera forward candidates vs (hero-camera) direction:')
d=(pos[0]-trans[0],pos[1]-trans[1],pos[2]-trans[2])
nd=math.sqrt(sum(v*v for v in d))
for name,vec in (('-col2(V)',(-V[8],-V[9],-V[10])),('+col2(V)',(V[8],V[9],V[10])),
                 ('-row2(V)',(-V[2],-V[6],-V[10])),('+row2(V)',(V[2],V[6],V[10]))):
    nv=math.sqrt(sum(v*v for v in vec)) or 1.0
    dot=sum(d[i]*vec[i] for i in range(3))/(nd*nv)
    print('  %-10s angle to hero %.1f deg'%(name,math.degrees(math.acos(max(-1,min(1,dot))))))

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
nx=(cx/abs(cz))*P[0]; ny=(cy/abs(cz))*P[5]
print('\nclient %dx%d -> ndc (%.3f, %.3f) screen (%.0f, %.0f) offCentre %.0f'%(
      W,H,nx,ny,CX*(1+nx),CY*(1-ny),math.hypot(nx*CX,ny*CY)))

k32.CloseHandle(ctypes.c_void_p(h))
