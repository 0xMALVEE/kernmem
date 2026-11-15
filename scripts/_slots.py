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
        h=hh; print('device via', dev); break
if not h: raise SystemExit('no device')

def ioctl(c,i,n):
    o=ctypes.create_string_buffer(n); r=w.DWORD(0)
    b=ctypes.create_string_buffer(i,len(i)) if i else None
    ok=k32.DeviceIoControl(h,c,b,len(i) if i else 0,o,n,ctypes.byref(r),None)
    return ok,o.raw[:r.value]

_,o=ioctl(0x80002008,struct.pack('<II',0,0)+'League of Legends.exe'.encode('utf-16-le').ljust(192,b'\x00'),8)
pid=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
print('pid:', pid)
if not pid: raise SystemExit('game not running - start a match')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
print('base: 0x%X'%base)
if not base: raise SystemExit('no base')

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

def nm(a):
    r=rd(a,48)
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
print('local: 0x%X'%(local or 0))

# enumerate the slot array itself, plus the manager list if it resolves
slots=[]
for i in range(8):
    e=u64(ARR+i*8)
    if not e:
        print('  null at slot %d'%i); break
    slots.append((i,e))

mg=u64(ARR)
begin=u64(mg) if mg else None
end=u64(mg+8) if mg else None
print('manager=0x%X begin=%s end=%s'%(mg or 0, hex(begin) if begin else None, hex(end) if end else None))

print('\n=== per-slot discovery (position + health, class agnostic) ===')
for i,e in slots:
    vt=u64(e) or 0
    print('\n[%d] 0x%X vt=0x%X inModule=%s team@0x1638=%s name=%r%s'%(
        i,e,vt,bool(base<=vt<base+0x4000000),i32(e+0x1638),nm(e+0x3F00),
        ' <== LOCAL' if e==local else ''))
    buf=rd(e,0x3000)
    if not buf:
        print('   unreadable'); continue
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
            if health(pair):
                best=(off,sub,t,pair); break
        if best: break
    if best:
        off,sub,t,pair=best
        print('   POSITION +0x%X/+0x%X (%.1f, %.1f, %.1f) hp=%.1f/%.1f'%(off,sub,t[0],t[1],t[2],pair[0],pair[1]))
    else:
        # fall back to a position without health
        for off in range(0,len(buf)-8,8):
            p=struct.unpack_from('<Q',buf,off)[0]
            if not pool(p): continue
            tb=rd(p,0x900)
            if not tb: continue
            for sub in range(0,0x8F0,4):
                t=struct.unpack_from('<3f',tb,sub)
                if not world(t): continue
                r2=rd(p+sub,12)
                if r2 and struct.unpack('<3f',r2)==t:
                    print('   position only +0x%X/+0x%X (%.1f, %.1f, %.1f)'%(off,sub,t[0],t[1],t[2]))
                    best=(off,sub,t,None); break
            if best: break
        if not best: print('   nothing')

if begin and end and end>begin:
    count=min((end-begin)//8, 0x2000)
    print('\n=== scanning manager list (%d entries) for team 1/2 with position+health ==='%count)
    found=0
    for k in range(count):
        e=u64(begin+k*8)
        if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
        if i32(e+0x1638) not in (1,2): continue
        buf=rd(e,0x3000)
        if not buf: continue
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
                if health(pair):
                    best=(off,sub,t,pair); break
            if best: break
        if not best: continue
        off,sub,t,pair=best
        found+=1
        print('  [%d] 0x%X team=%d name=%r pos=(%.1f,%.1f,%.1f) hp=%.1f/%.1f%s'%(
            k,e,i32(e+0x1638),nm(e+0x3F00),t[0],t[1],t[2],pair[0],pair[1],
            ' <== LOCAL' if e==local else ''))
        if found>12: break
    print('qualified: %d'%found)

k32.CloseHandle(ctypes.c_void_p(h))
