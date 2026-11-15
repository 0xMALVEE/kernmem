import ctypes, ctypes.wintypes as w, struct, math, time

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

OFF_LOCAL=0x1CFA7B0
OFF_LIST=0x1CB8550
OFF_TEAM=0x164
OFF_NAME=0x3890
CHAIN=0x200
SUBPOS=0x28

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

def asc(a,n=32):
    r=rd(a,n)
    if not r: return ''
    return r.split(b'\x00')[0].decode('ascii','replace')

def b1(a):
    r=rd(a,1)
    return r[0] if r else None

lst=u64(base+OFF_LIST)
arr=struct.unpack('<Q',rd(lst+8,8))[0]
cnt=struct.unpack('<i',rd(lst+0x10,4))[0]
local=u64(base+OFF_LOCAL)
print('count=%d local=0x%X'%(cnt,local or 0))

heroes=[]
for k in range(min(cnt,12)):
    e=u64(arr+k*8)
    if e and 0x10000000000<e<0x7FFFFFFFFFFF:
        heroes.append((k,e))

print('\n-- fixed chain pointer, 4 samples each 0.4s --')
for k,e in heroes:
    name=asc(e+OFF_NAME)
    team=b1(e+OFF_TEAM)
    ptr=u64(e+CHAIN)
    if not ptr or ptr<0x10000000000:
        print('  [%d] %-10r chain null'%(k,name)); continue
    addr=ptr+SUBPOS
    samples=[]
    for _ in range(4):
        r=rd(addr,12)
        samples.append(struct.unpack('<3f',r) if r else None)
        time.sleep(0.4)
    ok=all(s==samples[0] for s in samples)
    hp=rd(addr-8,8)
    hp=struct.unpack('<2f',hp) if hp else None
    d=math.dist(samples[0],samples[-1]) if (samples[0] and samples[-1]) else -1
    print('  [%d] %-10r team=%s ptr=0x%X stable=%s drift=%.1f'%(k,name,team,ptr,ok,d))
    for s in samples:
        print('      %s'%(s,))
    print('      hp(max@-8,cur@-4)=%s%s'%(hp,' <== LOCAL' if e==local else ''))

k32.CloseHandle(ctypes.c_void_p(h))
