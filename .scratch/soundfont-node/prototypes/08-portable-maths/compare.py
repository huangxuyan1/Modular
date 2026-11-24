"""PROTOTYPE, throwaway: soundfont-node ticket 08.

Compares two dumps of PROTOTYPE_maths_probe, from two builds, set by set: std:: against
std::, and the seam against the seam. Usage: python -I compare.py <a.bin> <b.bin>
"""
import struct
import sys


def read(path):
    sets = {}
    with open(path, "rb") as f:
        data = f.read()
    at = 0
    while at < len(data):
        (n,) = struct.unpack_from("<I", data, at)
        at += 4
        name = data[at:at + n].decode()
        at += n
        (count,) = struct.unpack_from("<Q", data, at)
        at += 8
        size = 8 * count
        inputs = data[at:at + size]
        at += size
        std = struct.unpack_from("<%dq" % count, data, at)
        at += size
        seam = struct.unpack_from("<%dq" % count, data, at)
        at += size
        sets[name] = (inputs, std, seam)
    return sets


def key(b):
    return b if b >= 0 else -(b & 0x7FFFFFFFFFFFFFFF)


def diff(a, b):
    count = 0
    worst = 0
    for x, y in zip(a, b):
        if x != y:
            count += 1
            worst = max(worst, abs(key(x) - key(y)))
    return count, worst


a = read(sys.argv[1])
b = read(sys.argv[2])
for name in a:
    ia, sa, pa = a[name]
    ib, sb, pb = b[name]
    same_inputs = ia == ib
    sc, sw = diff(sa, sb)
    pc, pw = diff(pa, pb)
    print("%-15s inputs %s  std differ %7d (%5.2f%%, max %d ulp)  seam differ %7d (max %d ulp)"
          % (name, "same" if same_inputs else "DIFFER", sc, 100.0 * sc / len(sa), sw, pc, pw))
