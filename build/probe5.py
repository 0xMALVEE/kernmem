import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "sdk"))
import vgb

GAME = "League of Legends.exe"
LO = 0x10000
HI = 0x7FFFFFFFFFFF

c = vgb.Client()
c.open()
c.require_version()
pid = c.pid_by_name(GAME)
base = c.main_base(pid)
print("pid", pid, "base", hex(base))


def u(fmt, a, n):
    return struct.unpack(fmt, c.read(pid, a, n))[0]


e = u("<I", base + 0x3C, 4)
print("e_lfanew", hex(e))
print("sig", c.read(pid, base + e, 4))
magic = u("<H", base + e + 0x18, 2)
print("magic", hex(magic), "PE32" if magic == 0x10B else ("PE32+" if magic == 0x20B else "?"))
print("size_of_image", hex(u("<I", base + e + 0x50, 4)))
print("image_base", hex(u("<Q", base + e + 0x30, 8)))


def window(label, off, radius=0x40):
    start = base + off - radius
    data = c.read(pid, start, radius * 2)
    print("\n== %s base+%#x (%d bytes)" % (label, off, len(data)))
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        if len(chunk) < 8:
            break
        q = struct.unpack("<Q", chunk)[0]
        lo32 = struct.unpack("<I", chunk[:4])[0]
        hi32 = struct.unpack("<I", chunk[4:])[0]
        m8 = "*" if LO <= q <= HI and q % 8 == 0 else " "
        m32 = "*" if LO <= lo32 <= HI and lo32 % 8 == 0 else " "
        print("  %+05x q=%016x%s lo=%08x%s hi=%08x" % (i - radius, q, m8, lo32, m32, hi32))


window("LocalPlayer", 0x1F304B0)
window("ObjectManager", 0x1EF0D28)
window("ViewMatrix", 0x1FBA090)
c.close()
