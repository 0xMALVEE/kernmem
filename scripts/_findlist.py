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

def nm(a):
    r=rd(a,48)
    if not r: return ''
    return r.decode('utf-16-le','replace').split('\x00')[0]

def valid(s):
    return bool(s) and 2 <= len(s) <= 24 and all(32 <= ord(c) < 127 for c in s)

local=u64(base+0x1CBB810)
print('local=0x%X'%(local or 0))
if not local: raise SystemExit('no local player')

print('\n-- every place in the globals holding the local hero pointer --')
hits=[]
for off in range(0x1C00000, 0x1D00000, 8):
    v=u64(base+off)
    if v==local:
        hits.append(base+off)
        print('  base+0x%X'%off)
print('hits=%d'%len(hits))

for addr in hits:
    print('\n=== array around base+0x%X ==='%(addr-base))
    ctx=rd(addr-0x40,0x200)
    if not ctx: continue
    for i in range(0,len(ctx),8):
        v=struct.unpack_from('<Q',ctx,i)[0]
        if not v or v<0x10000000000 or v>0x7FFFFFFFFFFF: continue
        vt=u64(v)
        if not vt or not (base<=vt<base+0x4000000): continue
        t=i32(v+0x1638)
        s=nm(v+0x3F00)
        mark=' <== LOCAL' if v==local else ''
        print('  %+d 0x%X team=%s name=%r%s'%(i-0x40,v,t,s,mark))

print('\n-- counts around each hit --')
for addr in hits:
    for d in (-0x10,-0x8,0x0,0x8,0x10):
        v=u64(addr+d)
        print('  base+0x%X %+d = 0x%X'%(addr-base,d,v or 0))

k32.CloseHandle(ctypes.c_void_p(h))
