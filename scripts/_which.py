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
u32.SetActiveWindow.argtypes=[w.HWND]
u32.SetFocus.argtypes=[w.HWND]
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
for attempt in range(4):
    fg=u32.GetForegroundWindow()
    if fg==game: break
    me=k32.GetCurrentThreadId()
    fgp=w.DWORD(); u32.GetWindowThreadProcessId(fg,ctypes.byref(fgp))
    tp=w.DWORD(); u32.GetWindowThreadProcessId(game,ctypes.byref(tp))
    u32.AttachThreadInput(me,tp,True); u32.AttachThreadInput(me,fgp.value,True)
    u32.ShowWindow(game,9); u32.BringWindowToTop(game)
    u32.SetActiveWindow(game); u32.SetForegroundWindow(game); u32.SetFocus(game)
    u32.AttachThreadInput(me,tp,False); u32.AttachThreadInput(me,fgp.value,False)
    time.sleep(1.0)
    print('attempt %d -> focused=%s'%(attempt, u32.GetForegroundWindow()==game))

print('focused: %s'%(u32.GetForegroundWindow()==game))

# resolve the signature fresh
hdr=rd(BASE,0x400)
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]
SIG=b'\x48\x8D\x0D\x00\x00\x00\x00\x0F\x10\x00'
MASK=[1,1,1,0,0,0,0,1,1,1]
hits=[]
CH=0x10000
for off in range(0,SIZE,CH):
    buf=rd(BASE+off,min(CH,SIZE-off))
    if not buf: continue
    i=0
    while True:
        i=buf.find(SIG[:3],i)
        if i<0 or i+len(SIG)>len(buf): break
        if all(not m or buf[i+k]==SIG[k] for k,m in enumerate(MASK)):
            hits.append(BASE+off+i)
        i+=1
print('signature hits: %d %s'%(len(hits),[hex(x-BASE) for x in hits]))

targets=[]
for hit in hits:
    t=hit+7+struct.unpack('<i',rd(hit+3,4))[0]
    targets.append(t)
    print('  hit base+0x%X -> target base+0x%X'%(hit-BASE,t-BASE))

HARD=BASE+0x1D7CC60
print('\nhardcoded base+0x1D7CC60')
print('  identical to a fresh target: %s'%(HARD in targets))

rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
local=u64(BASE+0x1CBB810)
pos=struct.unpack('<3f',rd(local+0x4584,12))
print('client %dx%d self (%.1f,%.1f,%.1f)'%(W,H,pos[0],pos[1],pos[2]))

cands=[('hard',HARD)]+[('sig%d'%i,t) for i,t in enumerate(targets)]
seen={}
for name,addr in cands:
    raw=rd(addr,0x80)
    if not raw:
        print('%s unreadable'%name); continue
    seen[name]=raw
    print('\n%s base+0x%X'%(name,addr-BASE))
    print('  crc=%08X'%(sum(raw)%0xFFFFFFFF))
    V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
    vx=pos[0]*V[0]+pos[1]*V[4]+pos[2]*V[8]+V[12]
    vy=pos[0]*V[1]+pos[1]*V[5]+pos[2]*V[9]+V[13]
    vz=pos[0]*V[2]+pos[1]*V[6]+pos[2]*V[10]+V[14]
    cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
    cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
    cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
    if abs(cw)<1e-3:
        print('  degenerate'); continue
    nx,ny=cx/cw,cy/cw
    print('  ndc=(%.3f, %.3f) screen=(%.0f, %.0f) on=%s'%(nx,ny,CX*(1+nx),CY*(1-ny),
          0<=CX*(1+nx)<W and 0<=CY*(1-ny)<H))

names=list(seen)
if len(names)>=2:
    for a in range(len(names)):
        for b in range(a+1,len(names)):
            d=sum(1 for x,y in zip(seen[names[a]],seen[names[b]]) if x!=y)
            print('\ndiff %s vs %s: %d differing bytes of %d'%(names[a],names[b],d,len(seen[names[a]])))

print('\n-- ndc_y drift over 6s, both --')
for i in range(6):
    line=''
    for name,addr in cands:
        raw=rd(addr,0x80)
        V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
        vx=pos[0]*V[0]+pos[1]*V[4]+pos[2]*V[8]+V[12]
        vy=pos[0]*V[1]+pos[1]*V[5]+pos[2]*V[9]+V[13]
        vz=pos[0]*V[2]+pos[1]*V[6]+pos[2]*V[10]+V[14]
        cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
        cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
        line += '%s=%.3f  ' % (name, cy/cw if abs(cw)>1e-3 else 999)
    print('  '+line)
    time.sleep(1.0)

k32.CloseHandle(ctypes.c_void_p(h))
