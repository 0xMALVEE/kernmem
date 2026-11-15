import ctypes, ctypes.wintypes as w, struct, math

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

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
pid=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
if not pid: raise SystemExit('game not running')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
print('pid=%d base=0x%X'%(pid,base))

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def nm(a):
    r=rd(a,64)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def pool(p):
    return p and 0x10000000000 < p < 0x7FFFFFFFFFFF and not (base <= p < base+0x4000000)

def world(t):
    if not t: return False
    x,y,z=t
    if not all(v==v and abs(v)<1e6 for v in t): return False
    return 300.0<x<20000.0 and 300.0<y<20000.0 and -800.0<z<3000.0

def health(pair):
    if not pair: return False
    cur,mx=pair
    if not (cur==cur and mx==mx): return False
    return 20.0 < mx < 8000.0 and -1.0 <= cur <= mx + 1.0

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
lptr=u64(local+0x5BA8) if local else None
lpos=struct.unpack('<3f',rd(lptr+0x2E4,12)) if lptr else None
mg=u64(ARR); begin=u64(mg); end=u64(mg+8)
count=(end-begin)//8
print('local=0x%X pos=%s count=%d'%(local or 0,lpos,count))

OFFS=(0x19F8,0x2858,0x29D8,0x1638)

objs=[]
for k in range(count):
    e=u64(begin+k*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e) or 0
    if not (base <= vt < base+0x4000000): continue
    if e in [o[1] for o in objs]: continue
    objs.append((k,e,vt))
print('unique in-module objects: %d'%len(objs))

data=[]
for k,e,vt in objs:
    buf=rd(e,0x3000)
    if not buf: continue
    teams={off:struct.unpack_from('<i',buf,off)[0] for off in OFFS if off+4<=len(buf)}
    if not any(v in (1,2) for v in teams.values()): continue
    best=None
    for off in range(0,len(buf)-8,8):
        p=struct.unpack_from('<Q',buf,off)[0]
        if not pool(p): continue
        tb=rd(p,0x900)
        if not tb: continue
        for sub in range(0,0x8F0,4):
            t=struct.unpack_from('<3f',tb,sub)
            if not world(t): continue
            r2=rd(p+sub,12)
            if not r2 or struct.unpack('<3f',r2)!=t: continue
            hp=rd(p+sub+0x98,8)
            pair=struct.unpack('<2f',hp) if hp else None
            if not health(pair): continue
            best=(off,sub,t,pair); break
        if best: break
    if best: data.append((k,e,teams,best,nm(e+0x3F00)))
print('objects passing team+pos+health: %d'%len(data))

for off in OFFS:
    mine_val = struct.unpack('<i',rd(local+off,4))[0] if local else None
    sel=[d for d in data if d[2].get(off) in (1,2)]
    allies=[d for d in sel if d[2].get(off)==mine_val]
    enemies=[d for d in sel if d[2].get(off)!=mine_val]
    print('\n=== +0x%X  total=%d allies=%d enemies=%d localVal=%s ==='%(
        off,len(sel),len(allies),len(enemies),mine_val))
    for k,e,teams,best,name in sel[:10]:
        o2,sub,t,pair=best
        d=math.dist(t,lpos) if lpos else 0
        print('  [%d] 0x%X team=%d %r pos=(%.1f,%.1f,%.1f) hp=%.1f/%.1f dist=%.0f%s'%(
            k,e,teams[off],name,t[0],t[1],t[2],pair[0],pair[1],d,' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
