import ctypes, ctypes.wintypes as w, struct

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

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

def name(a):
    r=rd(a,48)
    if not r: return None
    return r.decode('utf-16-le','replace').split('\x00')[0]

def valid(s):
    return bool(s) and 2 <= len(s) <= 24 and all(32 <= ord(c) < 127 for c in s)

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)
mg=u64(ARR)
print('local=0x%X manager=0x%X'%(local,mg or 0))

buf=rd(mg,0x300)
print('\n-- manager qwords --')
for i in range(0,len(buf),8):
    v=struct.unpack_from('<Q',buf,i)[0]
    tag=''
    if 0x10000000000<v<0x7FFFFFFFFFFF: tag=' pool' if not (base<=v<base+0x4000000) else ' image'
    print('  +0x%03X 0x%016X%s'%(i,v,tag))

print('\n-- every qword pair tried as (start,end) --')
qs=[struct.unpack_from('<Q',buf,i)[0] for i in range(0,len(buf),8)]
for ai in range(len(qs)):
    for bi in range(len(qs)):
        s,t=qs[ai],qs[bi]
        if ai==bi: continue
        if not (0x10000000000<s<0x7FFFFFFFFFFF): continue
        if not (0x10000000000<t<0x7FFFFFFFFFFF): continue
        if t<=s: continue
        n=(t-s)//8
        if n<1 or n>0x2000: continue
        named=0; heroish=0; samples=[]
        for k in range(min(n,0x300)):
            e=u64(s+k*8)
            if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
            vt=u64(e)
            if vt and base<=vt<base+0x4000000: heroish+=1
            nm=name(e+0x3F00)
            if valid(nm):
                named+=1
                if len(samples)<6: samples.append((k,e,nm,i32(e+0x1638)))
        if named or heroish>2:
            print('  pair +0x%03X..+0x%03X start=0x%X n=%d named=%d moduleVt=%d'%(ai*8,bi*8,s,n,named,heroish))
            for k,e,nm,tm in samples:
                print('     [%d] 0x%X %r team=%s'%(k,e,nm,tm))

k32.CloseHandle(ctypes.c_void_p(h))
