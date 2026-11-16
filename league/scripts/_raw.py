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
    print('open %-42s %s'%(dev,'OK' if hh and hh!=INV else 'fail %d'%ctypes.get_last_error()))
    if hh and hh!=INV:
        h=hh; break
if not h: raise SystemExit('no device')

def ioctl(c,i,n):
    o=ctypes.create_string_buffer(n); r=w.DWORD(0)
    b=ctypes.create_string_buffer(i,len(i)) if i else None
    ok=k32.DeviceIoControl(h,c,b,len(i) if i else 0,o,n,ctypes.byref(r),None)
    return ok,o.raw[:r.value]

_,o=ioctl(0x80002008,struct.pack('<II',0,0)+'League of Legends.exe'.encode('utf-16-le').ljust(192,b'\x00'),8)
pid=struct.unpack('<Q',o)[0] if o else 0
print('pid from driver:', pid)
if not pid: raise SystemExit('no pid')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o else 0
print('base from driver: 0x%X'%base)

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def hx(a,n):
    r=rd(a,n)
    if not r:
        print('  0x%X  READ FAIL'%a); return
    for i in range(0,len(r),16):
        chunk=r[i:i+16]
        print('  0x%X  %s  %s'%(a+i,' '.join('%02X'%b for b in chunk),
              ''.join(chr(b) if 32<=b<127 else '.' for b in chunk)))

print('\n-- header at base --')
hx(base,32)
print('\n-- LocalPlayer global base+0x1CBB810 --')
hx(base+0x1CBB810,16)
local=struct.unpack('<Q',rd(base+0x1CBB810,8) or b'\0'*8)[0]
print('local = 0x%X'%local)
print('\n-- bytes at local (first 0x60) --')
hx(local,0x60)
print('\n-- team +0x128 --')
hx(local+0x128,16)
print('\n-- name +0x3F00 --')
hx(local+0x3F00,32)
print('\n-- array global base+0x1CBB7F8, 8 entries --')
for i in range(8):
    hx(base+0x1CBB7F8+i*8,8)
print('\n-- manager chain --')
m=struct.unpack('<Q',rd(base+0x1CBB7F8,8) or b'\0'*8)[0]
print('manager = 0x%X'%m)
if m: hx(m,0x40)

k32.CloseHandle(ctypes.c_void_p(h))
