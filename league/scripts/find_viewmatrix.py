import ctypes, ctypes.wintypes as w, struct

GR=0x80000000; GW=0x40000000; OE=3; SA=7
INV=ctypes.c_void_p(-1).value
k32=ctypes.WinDLL('kernel32', use_last_error=True)
u32=ctypes.WinDLL('user32', use_last_error=True)
k32.CreateFileW.restype=ctypes.c_void_p
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k32.DeviceIoControl.restype=w.BOOL
k32.DeviceIoControl.argtypes=[ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.LPVOID]

h=k32.CreateFileW(r'\\?\GLOBALROOT\Device\Kernmem',GR|GW,SA,None,OE,0,None)
if not h or h==INV: raise SystemExit('open %d'%ctypes.get_last_error())

def ioctl(c,i,n):
    o=ctypes.create_string_buffer(n); r=w.DWORD(0)
    b=ctypes.create_string_buffer(i,len(i)) if i else None
    ok=k32.DeviceIoControl(h,c,b,len(i) if i else 0,o,n,ctypes.byref(r),None)
    return ok,o.raw[:r.value]

_,o=ioctl(0x80002008,struct.pack('<II',0,0)+'League of Legends.exe'.encode('utf-16-le').ljust(192,b'\x00'),8)
pid=struct.unpack('<Q',o)[0]
_,o=ioctl(0x80002024,struct.pack('<II',pid,0),8)
base=struct.unpack('<Q',o)[0]
print('pid=%d base=0x%X'%(pid,base))

def rd(a,n):
    ok,o=ioctl(0x80002010,struct.pack('<IIQ',pid,n,a),n)
    return o if ok else None

def u64(a):
    r=rd(a,8)
    return struct.unpack('<Q',r)[0] if r else None

def f32(a):
    r=rd(a,4)
    return struct.unpack('<f',r)[0] if r else None

hdr=rd(base,0x400)
if not hdr or hdr[:2]!=b'MZ': raise SystemExit('no MZ')
e=struct.unpack_from('<I',hdr,0x3c)[0]
size=struct.unpack_from('<I',hdr,e+0x18+56)[0]
print('image size 0x%X'%size)

SIG=b'\x48\x8D\x0D\x00\x00\x00\x00\x0F\x10\x00'
mask=[1,1,1,0,0,0,0,1,1,1]

hits=[]
CH=0x10000
for off in range(0,size,CH):
    buf=rd(base+off,min(CH,size-off))
    if not buf: continue
    i=0
    while True:
        i=buf.find(SIG[:3],i)
        if i<0: break
        if i+len(SIG)>len(buf): break
        ok=True
        for k,m in enumerate(mask):
            if m and buf[i+k]!=SIG[k]: ok=False; break
        if ok: hits.append(base+off+i)
        i+=1
print('signature hits: %d'%len(hits))

win=u32.FindWindowW(None,'League of Legends (TM) Client') or u32.FindWindowW(None,None)
rect=w.RECT()
if win: u32.GetClientRect(win,ctypes.byref(rect))
print('client %dx%d'%(rect.right,rect.bottom))

local=u64(base+0x1CBB810)
pos=(f32(local+0x4584),f32(local+0x4584+4),f32(local+0x4584+8))
print('local pos %s'%(pos,))

for hit in hits:
    disp=struct.unpack('<i',rd(hit+3,4))[0]
    target=hit+7+disp
    raw=rd(target,0x80)
    if not raw: 
        print('  0x%X -> 0x%X unreadable'%(hit,target)); continue
    view=list(struct.unpack_from('<16f',raw,0)); proj=list(struct.unpack_from('<16f',raw,0x40))
    fin=all(v==v and abs(v)<1e6 for v in view+proj)
    zero=all(abs(v)<1e-9 for v in view)
    vp=[sum(view[r*4+k]*proj[c*4+k] for k in range(4)) for r in range(4) for c in range(4)]
    cX=pos[0]*vp[0]+pos[1]*vp[4]+pos[2]*vp[8]+vp[12]
    cY=pos[0]*vp[1]+pos[1]*vp[5]+pos[2]*vp[9]+vp[13]
    cW=pos[0]*vp[3]+pos[1]*vp[7]+pos[2]*vp[11]+vp[15]
    sx=sy=cW
    if abs(cW)>1e-6:
        sx=rect.right*0.5*(1+cX/cW); sy=rect.bottom*0.5*(1-cY/cW)
    print('  hit 0x%X -> matrix 0x%X off 0x%X finite=%s zero=%s w=%.2f screen=(%.0f, %.0f)'%(
        hit,target,target-base,fin,zero,cW,sx,sy))

k32.CloseHandle(ctypes.c_void_p(h))
