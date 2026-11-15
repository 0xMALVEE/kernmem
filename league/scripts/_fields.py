import ctypes, ctypes.wintypes as w, struct, math

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

h=None
for dev in (r'\\.\VanguardBypass', r'\\?\GLOBALROOT\Device\VanguardBypass'):
    hh=k32.CreateFileW(dev,GR|GW,SA,None,OE,0,None)
    print('open %-40s %s'%(dev,'OK' if hh and hh!=INV else 'fail %d'%ctypes.get_last_error()))
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
print('pid=%d base=0x%X'%(pid,base))

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r else None

game=u32.FindWindowW(None,'League of Legends (TM) Client')
rect=w.RECT(); u32.GetClientRect(game,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2
print('client %dx%d centre (%.0f,%.0f)'%(W,H,CX,CY))

raw=rd(base+0x1D7CC60,0x80)
V=list(struct.unpack_from('<16f',raw,0)); P=list(struct.unpack_from('<16f',raw,0x40))
M=[sum(V[r*4+k]*P[k*4+c] for k in range(4)) for r in range(4) for c in range(4)]

def project(p):
    px,py,pz=p
    cx=px*M[0]+py*M[4]+pz*M[8]+M[12]
    cy=px*M[1]+py*M[5]+pz*M[9]+M[13]
    cw=px*M[3]+py*M[7]+pz*M[11]+M[15]
    if abs(cw)<1e-3: return None
    return (CX*(1+cx/cw), CY*(1-cy/cw))

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
print('local=0x%X'%local)

SIZE=0x6000
buf=rd(local,SIZE)
if not buf: raise SystemExit('local unreadable')
print('local bytes: %d'%len(buf))

print('\n-- triples on local that project near centre, ring circularity --')
rows=[]
for off in range(0,len(buf)-12,4):
    p=struct.unpack_from('<3f',buf,off)
    if not all(v==v and abs(v)<1e7 for v in p): continue
    if not (200.0<p[0]<30000.0 and 200.0<p[1]<30000.0 and -4000.0<p[2]<4000.0): continue
    s=project(p)
    if not s: continue
    offc=math.hypot(s[0]-CX,s[1]-CY)
    if offc>900: continue
    pts=[]
    for i in range(24):
        a=2*math.pi*i/24
        q=project((p[0]+700*math.cos(a), p[1]+700*math.sin(a), p[2]))
        if q: pts.append(q)
    if len(pts)<18: continue
    xs=[q[0] for q in pts]; ys=[q[1] for q in pts]
    bw=max(xs)-min(xs); bh=max(ys)-min(ys)
    if bw<10 or bh<10: continue
    circ=abs(bw-bh)/max(bw,bh)
    on=sum(1 for q in pts if 0<=q[0]<W and 0<=q[1]<H)
    rows.append((offc+circ*300+(24-on)*10, off, p, s, offc, circ, (bw+bh)/4, on))
rows.sort()
for r in rows[:6]:
    print('  score=%6.0f +0x%04X world=(%.1f, %.1f, %.1f) screen=(%.0f, %.0f) off=%.0f circ=%.3f rad=%.1f on=%d'%(
        r[0],r[1],r[2][0],r[2][1],r[2][2],r[3][0],r[3][1],r[4],r[5],r[6],r[7]))
if not rows: print('  none')

print('\n-- int pairs 1/2 in first 0x300 across objects --')
objs=[]
for i in range(8):
    e=u64(ARR+i*8)
    if e and 0x10000000000<e<0x7FFFFFFFFFFF:
        b=rd(e,0x300)
        if b:
            nm=rd(e+0x3F00,32)
            objs.append((i,e,b,nm.decode('utf-16-le','replace').split('\x00')[0] if nm else ''))
for i,e,b,nm in objs:
    print('  [%d] 0x%X %r'%(i,e,nm))
for ai in range(len(objs)):
    for bi in range(ai+1,len(objs)):
        a=objs[ai][2]; b=objs[bi][2]
        for off in range(0,0x300,4):
            av=struct.unpack_from('<i',a,off)[0]; bv=struct.unpack_from('<i',b,off)[0]
            if {av,bv}=={1,2}:
                print('  TEAM? +0x%X  [%d]=%d [%d]=%d'%(off,objs[ai][0],av,objs[bi][0],bv))

k32.CloseHandle(ctypes.c_void_p(h))
