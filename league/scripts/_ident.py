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

def f32(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r and len(r)==4 else None

def nm(a):
    r=rd(a+0x3F00,48)
    return r.decode('utf-16-le','replace').split('\x00')[0] if r else None

ARR=base+0x1CBB7F8
local=u64(base+0x1CBB810)

objs=[]
for i in range(0x400):
    e=u64(ARR+i*8)
    if not e or not (0x10000000000<e<0x7FFFFFFFFFFF): continue
    vt=u64(e)
    if not vt or not (base<=vt<base+0x3000000): continue
    objs.append((i,e,vt))
print('module-vtable objects: %d'%len(objs))
for i,e,vt in objs:
    print('  [%d] 0x%X vt=0x%X team=%s name=%r%s'%(i,e,vt,i32(e+0x1638),nm(e),
          ' <== LOCAL' if e==local else ''))

print('\nlocal vt=0x%X'%(u64(local) or 0))
for i,e,vt in objs:
    if e==local: continue
    print('  0x%X same class as local: %s'%(e, vt==u64(local)))

for i,e,vt in objs:
    print('\n== [%d] 0x%X %r =='%(i,e,nm(e)))
    for co in (0x5BA8,0x1790,0x2AD0,0x3098,0x44A0):
        q=u64(e+co)
        print('  qword at +0x%X = 0x%X%s'%(co,q or 0,
              '' if not q else ('  (in image)' if base<=q<base+0x4000000 else '')))
        if q and 0x10000000000<q<0x7FFFFFFFFFFF and not (base<=q<base+0x4000000):
            tgt=rd(q+0x2E4,0x140)
            if tgt:
                trip=struct.unpack_from('<3f',tgt,0)
                hp=struct.unpack_from('<2f',tgt,0x98)
                print('      +0x2E4 triple=(%.2f, %.2f, %.2f)  hp/maxhp at +0x98=(%.1f, %.1f)'%(
                      trip[0],trip[1],trip[2],hp[0],hp[1]))
                ints=[struct.unpack_from('<i',tgt,o)[0] for o in range(0xB0,0xD0,4)]
                print('      ints +0xB0..+0xD0: %s'%ints)

print('\n-- level hunt: ints 1..18 on both, same offset, in first 0x2000 --')
for i,e,vt in objs:
    if e==local: continue
    a=rd(local,0x2000); b=rd(e,0x2000)
    if not a or not b: continue
    hits=0
    for off in range(0,0x2000-4,4):
        av=struct.unpack_from('<i',a,off)[0]; bv=struct.unpack_from('<i',b,off)[0]
        if 1<=av<=18 and 1<=bv<=18 and struct.unpack_from('<f',a,off)[0]<0.001 and struct.unpack_from('<f',b,off)[0]<0.001:
            print('  +0x%X local=%d other=%d'%(off,av,bv))
            hits+=1
            if hits>8: break

k32.CloseHandle(ctypes.c_void_p(h))
