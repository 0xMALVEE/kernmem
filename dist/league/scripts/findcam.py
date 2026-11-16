import ctypes, ctypes.wintypes as w, struct, math, time

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
u32=ctypes.WinDLL('user32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

h=k32.CreateFileW(r'\\?\GLOBALROOT\Device\Kernmem',GR|GW,SA,None,OE,0,None)
if not h or h==INV: raise SystemExit('open %d'%ctypes.get_last_error())

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

hdr=rd(BASE,0x400)
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]
print('base=0x%X size=0x%X'%(BASE,SIZE))

local=u64(BASE+0x1CBB810)
print('-- liveness: hero pos vs signature matrix, 4 samples --')
prevp=prev=None
for i in range(4):
    p=rd(local+0x4584,12)
    m=rd(BASE+0x1D7CC60,0x80)
    pc=sum(p) if p else 0; mc=sum(m)%0xFFFFFFFF if m else 0
    print('  %d pos=%s posChanged=%s matCrc=%08X matChanged=%s'%(i,
        tuple(round(v,2) for v in struct.unpack('<3f',p)), prevp is not None and pc!=prevp,
        mc, prev is not None and mc!=prev))
    prevp,prev=pc,mc
    time.sleep(0.8)

x,y,z=struct.unpack('<3f',rd(local+0x4584,12))

hits=[]
CH=0x20000
for off in range(0,SIZE,CH):
    buf=rd(BASE+off,min(CH,SIZE-off))
    if not buf: continue
    for i in range(0,len(buf)-0x80,0x10):
        if abs(struct.unpack_from('<f',buf,i+0x3c)[0]-1.0)>0.01: continue
        m=struct.unpack_from('<16f',buf,i)
        if not all(v==v and abs(v)<1e7 for v in m): continue
        ok=True
        for r in range(3):
            n=math.sqrt(m[r*4]**2+m[r*4+1]**2+m[r*4+2]**2)
            if abs(n-1.0)>0.06: ok=False; break
        if not ok: continue
        pr=struct.unpack_from('<16f',buf,i+0x40) if i+0x80<=len(buf) else None
        if not pr: continue
        if abs(pr[15])>0.06 or abs(pr[11]-1.0)>0.06 or pr[0]<0.2 or pr[5]<0.2: continue
        hits.append((BASE+off+i,m,pr))
print('\nview+proj candidates: %d'%len(hits))

hwnd=u32.FindWindowW(None,'League of Legends (TM) Client')
rect=w.RECT(); u32.GetClientRect(hwnd,ctypes.byref(rect))
W,H=rect.right,rect.bottom
CX,CY=W/2,H/2

rows=[]
for addr,V,P in hits:
    cam=tuple(-(V[12]*V[r]+V[13]*V[4+r]+V[14]*V[8+r]) for r in range(3))
    d=math.dist(cam,(x,y,z))
    if d>20000: continue
    vx=x*V[0]+y*V[4]+z*V[8]+V[12]
    vy=x*V[1]+y*V[5]+z*V[9]+V[13]
    vz=x*V[2]+y*V[6]+z*V[10]+V[14]
    cx=vx*P[0]+vy*P[4]+vz*P[8]+P[12]
    cy=vx*P[1]+vy*P[5]+vz*P[9]+P[13]
    cw=vx*P[3]+vy*P[7]+vz*P[11]+P[15]
    if abs(cw)<1e-3: continue
    sx=CX*(1+cx/cw); sy=CY*(1-cy/cw)
    offc=math.hypot(sx-CX,sy-CY)
    rows.append((offc+d*0.05,addr,d,sx,sy,cw))

rows.sort()
print('\n  score   address     camDist  screen          w')
for r in rows[:10]:
    print('  %6.0f  0x%X  %7.0f  (%6.0f,%6.0f)  %.0f'%r)

if rows:
    _,addr,V,P=0,None,None,None
    addr=rows[0][1]
    V=list(struct.unpack('<16f',rd(addr,0x40)))
    P=list(struct.unpack('<16f',rd(addr+0x40,0x40)))
    def proj(p):
        px,py,pz=p
        a=px*V[0]+py*V[4]+pz*V[8]+V[12]
        b=px*V[1]+py*V[5]+pz*V[9]+V[13]
        c=px*V[2]+py*V[6]+pz*V[10]+V[14]
        e=a*P[0]+b*P[4]+c*P[8]+P[12]
        f=a*P[1]+b*P[5]+c*P[9]+P[13]
        g=a*P[3]+b*P[7]+c*P[11]+P[15]
        if abs(g)<1e-3: return None
        return (CX*(1+e/g), CY*(1-f/g))
    pts=[proj((x+700*math.cos(2*math.pi*i/64), y+700*math.sin(2*math.pi*i/64), z)) for i in range(64)]
    pts=[p for p in pts if p]
    if pts:
        xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
        print('\nbest 0x%X ring: %d/64 bbox %.0fx%.0f onscreen %d'%(addr,len(pts),
              max(xs)-min(xs),max(ys)-min(ys),sum(1 for p in pts if 0<=p[0]<W and 0<=p[1]<H)))

k32.CloseHandle(ctypes.c_void_p(h))
