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
pid=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0
if not pid: raise SystemExit('game not running')
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0] if o and len(o)>=8 else 0

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r and len(r)==8 else None

def i32(a):
    r=rd(a,4)
    return struct.unpack('<i',r)[0] if r and len(r)==4 else None

lst=u64(base+0x1CB8550)
arr=u64(lst+8)
cnt=i32(lst+0x10)
print('heroes=%s'%cnt)

heroes=[]
for k in range(min(cnt,10)):
    e=u64(arr+k*8)
    if e and 0x10000000000<e<0x7FFFFFFFFFFF:
        heroes.append((k,e))

# for every candidate offset, require a plausible max on 3+ heroes
print('\n offset  hits  samples')
for off in range(-0x80, 0x120, 4):
    hits=0
    samples=[]
    for k,e in heroes:
        r=rd(e+0xA0+off,8)
        if not r: continue
        cur,mx=struct.unpack('<2f',r)
        if not (cur==cur and mx==mx): continue
        if not (50.0 < mx < 12000.0): continue
        if not (-1.0 <= cur <= mx*1.2): continue
        hits+=1
        if len(samples)<3: samples.append('%.1f/%.1f'%(cur,mx))
    if hits >= 3:
        print('  %+5d  %d    %s'%(off,hits,samples))

k32.CloseHandle(ctypes.c_void_p(h))
