import ctypes, ctypes.wintypes as w, struct, time

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
u32.IsWindowVisible.argtypes=[w.HWND]
u32.IsIconic.argtypes=[w.HWND]
u32.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u32.GetWindowRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]

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

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
wr=w.RECT(); cr=w.RECT()
u32.GetWindowRect(game,ctypes.byref(wr)); u32.GetClientRect(game,ctypes.byref(cr))
print('hwnd=0x%X visible=%s iconic=%s foreground=%s'%(game,
      bool(u32.IsWindowVisible(game)),bool(u32.IsIconic(game)),
      u32.GetForegroundWindow()==game))
print('window %d,%d %dx%d  client %dx%d'%(wr.left,wr.top,wr.right-wr.left,wr.bottom-wr.top,cr.right,cr.bottom))

k32.GetProcessTimes.argtypes=[w.HANDLE,ctypes.POINTER(w.FILETIME),ctypes.POINTER(w.FILETIME),ctypes.POINTER(w.FILETIME),ctypes.POINTER(w.FILETIME)]
proc=k32.OpenProcess(0x0400, False, pid)

def cpu():
    if not proc: return 0
    a,b,c,d=w.FILETIME(),w.FILETIME(),w.FILETIME(),w.FILETIME()
    k32.GetProcessTimes(proc,ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d))
    return (c.dwHighDateTime<<32)|c.dwLowDateTime

local=u64(base+0x1CBB810)
CAM=base+0x1D7CC60
print('local=0x%X'%local)

print('\n-- 12 samples: hero pos, camera, cpu --')
pp=pm=pc=None
posch=camch=cpuch=0
for i in range(12):
    p=f3((u64(local+0x5BA8) or 0)+0x2E4)
    m=rd(CAM,0x80)
    c=cpu()
    pcs=sum(p) if p else 0
    mcs=sum(m)%0xFFFFFFFF if m else 0
    if pp is not None and pcs!=pp: posch+=1
    if pm is not None and mcs!=pm: camch+=1
    if pc is not None and c!=pc: cpuch+=1
    pp,pm,pc=pcs,mcs,c
    if i in (0,6,11):
        print('  %2d pos=%s camCrc=%08X cpu=%d'%(i,tuple(round(v,1) for v in p) if p else None,mcs,c))
    time.sleep(0.5)
print('  posChanged=%d/11 camChanged=%d/11 cpuChanged=%d/11'%(posch,camch,cpuch))

print('\n-- window placement --')
class WINDOWPLACEMENT(ctypes.Structure):
    _fields_=[('length',w.UINT),('flags',w.UINT),('showCmd',w.UINT),
              ('ptMinPosition',w.POINT),('ptMaxPosition',w.POINT),('rcNormalPosition',w.RECT)]
wp=WINDOWPLACEMENT(); wp.length=ctypes.sizeof(wp)
if u32.GetWindowPlacement(game,ctypes.byref(wp)):
    print('  showCmd=%d flags=0x%X'%(wp.showCmd,wp.flags))

if proc: k32.CloseHandle(proc)
k32.CloseHandle(ctypes.c_void_p(h))
