import ctypes, ctypes.wintypes as w, struct, math, time

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

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

local=u64(base+0x1CBB810)
ptr=u64(local+0x5BA8)
pos=struct.unpack('<3f',rd(ptr+0x2E4,12))
print('base=0x%X pos=%s'%(base,pos))

hdr=rd(base,0x400)
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]
print('image 0x%X'%SIZE)

hits=[]
CH=0x20000
for off in range(0,SIZE,CH):
    buf=rd(base+off,min(CH,SIZE-off))
    if not buf: continue
    for i in range(0,len(buf)-0x40,0x10):
        if abs(struct.unpack_from('<f',buf,i+0x3c)[0]-1.0)>0.02: continue
        m=struct.unpack_from('<16f',buf,i)
        if not all(v==v and abs(v)<1e8 for v in m): continue
        if any(abs(m[k])>1e-4 for k in (3,7,11)): continue
        rows=[(m[0],m[1],m[2]),(m[4],m[5],m[6]),(m[8],m[9],m[10])]
        ok=True
        for r in rows:
            n=math.sqrt(sum(v*v for v in r))
            if abs(n-1.0)>0.02: ok=False; break
        if not ok: continue
        d=sum(rows[0][k]*rows[1][k] for k in range(3))
        if abs(d)>0.02: continue
        t=(m[12],m[13],m[14])
        cam=tuple(-(t[0]*rows[r][0]+t[1]*rows[r][1]+t[2]*rows[r][2]) for r in range(3))
        dist=math.dist(cam,pos)
        if dist<50 or dist>40000: continue
        fwd=(-rows[2][0],-rows[2][1],-rows[2][2])
        dv=(pos[0]-cam[0],pos[1]-cam[1],pos[2]-cam[2])
        nd=math.sqrt(sum(v*v for v in dv)) or 1.0
        dot=sum(fwd[k]*dv[k] for k in range(3))/nd
        ang=math.degrees(math.acos(max(-1.0,min(1.0,dot))))
        hits.append((ang, base+off+i, cam, dist, rows, t))

hits.sort()
print('\ncamera-like blocks: %d'%len(hits))
print('  angle  address        camDist  camera')
for a,addr,cam,dist,rows,t in hits[:8]:
    print('  %5.1f  0x%X  %7.0f  (%0.0f, %0.0f, %0.0f)'%(a,addr-base,dist,cam[0],cam[1],cam[2]))

print('\n-- projection block near each of the best 3 --')
for a,addr,cam,dist,rows,t in hits[:3]:
    print('\n  candidate base+0x%X angle %.1f'%(addr-base,a))
    for d in (0x40,-0x40,0x80,-0x80,0xC0,-0xC0,0x100,-0x100):
        blk=rd(addr+d,0x40)
        if not blk: continue
        pm=struct.unpack('<16f',blk)
        if not all(v==v and abs(v)<1e8 for v in pm): continue
        if abs(pm[11]-1.0)>0.05 and abs(pm[3]-1.0)>0.05: continue
        if pm[0]<0.2 or pm[5]<0.2: continue
        print('    proj at %+0x  fovH=%.1f fovV=%.1f  m11=%.2f m15=%.2f'%(
            d, 2*math.degrees(math.atan(1/pm[0])), 2*math.degrees(math.atan(1/pm[5])), pm[11], pm[15]))

k32.CloseHandle(ctypes.c_void_p(h))
