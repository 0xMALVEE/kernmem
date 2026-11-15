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

try:
    u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    try:
        ctypes.WinDLL('shcore').SetProcessDpiAwareness(2)
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

def f3(a):
    r=rd(a,12)
    return struct.unpack('<3f',r) if r and len(r)==12 else None

print('pid=%d base=0x%X'%(pid,base))

hdr=rd(base,0x400)
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]
print('image size 0x%X'%SIZE)

SIG=b'\x48\x8D\x0D\x00\x00\x00\x00\x0F\x10\x00'
MASK=[1,1,1,0,0,0,0,1,1,1]
hits=[]
CH=0x10000
for off in range(0,SIZE,CH):
    buf=rd(base+off,min(CH,SIZE-off))
    if not buf: continue
    i=0
    while True:
        i=buf.find(SIG[:3],i)
        if i<0 or i+len(SIG)>len(buf): break
        if all(not m or buf[i+k]==SIG[k] for k,m in enumerate(MASK)):
            hits.append(base+off+i)
        i+=1
print('signature hits: %d %s'%(len(hits),[hex(x-base) for x in hits]))

targets=[]
for hit in hits:
    t=hit+7+struct.unpack('<i',rd(hit+3,4))[0]
    targets.append(t)
    print('  hit base+0x%X -> target base+0x%X'%(hit-base,t-base))

HARD=base+0x1D7CC60
print('hardcoded base+0x1D7CC60 present in fresh targets: %s'%(HARD in targets))
if HARD not in targets:
    targets.append(HARD)

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
pos=f3((u64(local+0x5BA8) or 0)+0x2E4)
print('local=0x%X chainpos=%s'%(local,pos))
if not pos: raise SystemExit('no position')

game=u32.FindWindowW(None,'League of Legends (TM) Client')
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('client %dx%d centre (%.0f,%.0f)'%(W,H,CX,CY))

for addr in targets:
    label='base+0x%X'%(addr-base)
    print('\n=== %s ==='%label)
    a=rd(addr,0x80)
    b=rd(addr,0x80)
    time.sleep(0.8)
    c=rd(addr,0x80)
    if not a or not b or not c:
        print('  unreadable'); continue
    print('  live: %s (crc %08X -> %08X)'%(a!=b or b!=c, sum(a)%0xFFFFFFFF, sum(c)%0xFFFFFFFF))
    V=list(struct.unpack_from('<16f',c,0)); P=list(struct.unpack_from('<16f',c,0x40))
    M=[sum(V[r*4+k]*P[k*4+x] for k in range(4)) for r in range(4) for x in range(4)]
    px,py,pz=pos
    cx=px*M[0]+py*M[4]+pz*M[8]+M[12]
    cy=px*M[1]+py*M[5]+pz*M[9]+M[13]
    cw=px*M[3]+py*M[7]+pz*M[11]+M[15]
    if abs(cw)<1e-3:
        print('  degenerate w'); continue
    sx=CX*(1+cx/cw); sy=CY*(1-cy/cw)
    print('  screen=(%.0f, %.0f) offCentre=%.0f on=%s w=%.0f'%(sx,sy,
          math.hypot(sx-CX,sy-CY),0<=sx<W and 0<=sy<H,cw))
    pts=[]
    for i in range(48):
        ang=2*math.pi*i/48
        q=(px+700*math.cos(ang), py+700*math.sin(ang), pz)
        qx=q[0]*M[0]+q[1]*M[4]+q[2]*M[8]+M[12]
        qy=q[0]*M[1]+q[1]*M[5]+q[2]*M[9]+M[13]
        qw=q[0]*M[3]+q[1]*M[7]+q[2]*M[11]+M[15]
        if abs(qw)>1e-3:
            pts.append((CX*(1+qx/qw), CY*(1-qy/qw)))
    if pts:
        xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
        print('  ring %d/48 bbox %.0fx%.0f on=%d'%(len(pts),max(xs)-min(xs),max(ys)-min(ys),
              sum(1 for p in pts if 0<=p[0]<W and 0<=p[1]<H)))

k32.CloseHandle(ctypes.c_void_p(h))
