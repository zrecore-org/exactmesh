# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""Minimal MSH1 reader/writer and exact (Fraction) helpers for the V2 CLI checks (docs/PLAN.md, "V2"). Run with `python3 -I`."""
import struct
from fractions import Fraction

def read_msh(path):
    d = open(path, "rb").read()
    magic, nv, nt = struct.unpack_from("<III", d, 0)
    assert magic == 0x3148534D, path
    v = struct.unpack_from("<%dd" % (3 * nv), d, 12)
    t = struct.unpack_from("<%dI" % (3 * nt), d, 12 + 24 * nv)
    return list(v), list(t)

def write_msh(path, v, t):
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x3148534D, len(v) // 3, len(t) // 3))
        f.write(struct.pack("<%dd" % len(v), *v)); f.write(struct.pack("<%dI" % len(t), *t))

_BOXT = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4), (3, 7, 6), (3, 6, 2), (0, 4, 7), (0, 7, 3), (1, 2, 6), (1, 6, 5)]   # outward, counter-clockwise (oracle/compare.cpp)
def box(lo, hi, inverted=False):
    v = []
    for z in (0, 1):
        Z = hi[2] if z else lo[2]
        v += [lo[0], lo[1], Z, hi[0], lo[1], Z, hi[0], hi[1], Z, lo[0], hi[1], Z]
    t = []
    for a, b, c in _BOXT: t += [a, c, b] if inverted else [a, b, c]
    return v, t

def append(m1, m2):
    v1, t1 = m1; v2, t2 = m2; o = len(v1) // 3
    return v1 + v2, t1 + [i + o for i in t2]

def vol6(v, t, tris=None):
    """exact 6 x signed volume of the triangles `tris` (default: all) of a mesh, as a Fraction"""
    s = Fraction(0)
    for k in (range(len(t) // 3) if tris is None else tris):
        a, b, c = [[Fraction(v[3 * t[3 * k + j] + d]) for d in range(3)] for j in range(3)]
        s += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) + a[2] * (b[0] * c[1] - b[1] * c[0])
    return s

def components(v, t):
    """edge/vertex-index connected triangle sets (generator cases keep separate vertex arrays per component); list of lists of triangle indices"""
    par = list(range(len(v) // 3))
    def f(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for k in range(len(t) // 3):
        a, b, c = t[3 * k:3 * k + 3]; par[f(b)] = f(a); par[f(c)] = f(a)
    g = {}
    for k in range(len(t) // 3): g.setdefault(f(t[3 * k]), []).append(k)
    return [g[k] for k in sorted(g)]

def dyadic(fr):
    """Fraction (denominator a power of two) -> 'num*2^e' as read by oracle/compare.cpp parseDyadic"""
    fr = Fraction(fr); d = fr.denominator
    assert d & (d - 1) == 0
    return "%d*2^%d" % (fr.numerator, -(d.bit_length() - 1))
