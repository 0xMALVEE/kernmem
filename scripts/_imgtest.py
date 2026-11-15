import ctypes, ctypes.wintypes as w, struct

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

CHUNK=0x10000

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

# direct read of the known-good site
KNOWN=base+0x10F87EF
direct=rd(KNOWN-8, 24)
print('direct bytes at known site: %s'%(direct.hex() if direct else None))

# rebuild the image and compare the same site
hdr=rd(base,0x400)
SIZE=struct.unpack_from('<I',hdr,struct.unpack_from('<I',hdr,0x3c)[0]+0x18+56)[0]
img=bytearray()
off=0
while off < SIZE:
    want=min(CHUNK, SIZE-off)
    b=rd(base+off, want)
    img += b if b else b'\x00'*want
    if b and len(b)<want:
        img += b'\x00'*(want-len(b))
    off += want
img=bytes(img)
print('image len %d, first 2 bytes %r'%(len(img), img[:2]))
print('image bytes at known site: %s'%img[0x10F87EF-8:0x10F87EF+16].hex())
print('match direct: %s'%(img[0x10F87EF-8:0x10F87EF+16] == direct))

print('\n-- counting common opcodes in the buffer --')
for label,pat in (('48 8B 0D', b'\x48\x8B\x0D'), ('48 8D 0D', b'\x48\x8D\x0D'),
                  ('0F B6 88', b'\x0F\xB6\x88'), ('F3 0F 10 80', b'\xF3\x0F\x10\x80'),
                  ('MZ', b'MZ'), ('zero runs', b'\x00'*16)):
    print('  %-12s %d'%(label, img.count(pat)))

print('\n-- bytes at base+0 (should be MZ header) --')
print('  %s'%img[:32].hex())

k32.CloseHandle(ctypes.c_void_p(h))
