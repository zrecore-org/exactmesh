#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V3 (docs/PLAN.md, "V3"), part (b): NON-cavity control pairs with one-ulp / subnormal features, same corpus layout as make_cavity_pairs.py so that the unchanged scorer (score_fidelity2.py / mesh_compare) judges them.
usage: python3 -I make_control_pairs.py <out dir>      writes <out>/manifest.tsv and <out>/adversarial/<id>_{A,B}.msh. No random numbers. All operands are unions of positive axis-aligned boxes, so every expectation is an exact Fraction.
Families (reference box R = [0,4]^3, l = smallest subnormal, u = nextafter(4,0), B variants = the fractions of R used by make_cavity_pairs.py):
  S1_sub / S1_ulp : one positive box {l,1,1}..{2,2,2} or {1,1,1}..{u,u,u} (the cavity solids of cases 050/052 and 051/053 WITHOUT the outer box), 4 B variants.
  S2_plate        : plate of thickness t (l, 2^-51, 2^-100) along axis a at the origin end, plus t = 2^-51 at the far end [u,4]; scale 1 and 2^30 (exercises the pipeline scale path); 4 B variants.
  S3_gap          : two boxes separated by a gap of one ulp (gap l at the origin, gap 2^-51 at 2) along axis a, 4 B variants.
  S4_overlap      : operands overlapping by one ulp (A=[0,4]^3, B axis interval [u,6]; and A axis [l,4], B axis [-2,2l]), no variants (3 axes each).
Volumes are 6 x volume (convention of exactVolumeTimes6)."""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../v1"))
from fractions import Fraction
from meshpy import *

out = sys.argv[1]
os.makedirs(os.path.join(out, "adversarial"), exist_ok=True)
L = 4.0; l = math.nextafter(0.0, 1.0); u = math.nextafter(4.0, 0.0); ulp2 = 2.0 ** -51
VARIANTS = {"contain": [(-0.5, 1.5)] * 3, "disjoint": [(1.5, 2.5), (0.25, 0.75), (0.25, 0.75)],
            "cutA": [(0.37, 1.30), (-0.2, 0.61), (0.13, 1.40)], "cutB": [(0.113, 0.707), (0.051, 1.109), (-0.131, 0.497)]}   # identical to make_cavity_pairs.py
man = ["id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff"]
def sc(x, k): return math.ldexp(x, k) if k else x
def vbox(b): return 6 * (Fraction(b[1][0]) - Fraction(b[0][0])) * (Fraction(b[1][1]) - Fraction(b[0][1])) * (Fraction(b[1][2]) - Fraction(b[0][2]))
def ov(a, b):
    e = [max(Fraction(0), min(Fraction(a[1][d]), Fraction(b[1][d])) - max(Fraction(a[0][d]), Fraction(b[0][d]))) for d in range(3)]
    return 6 * e[0] * e[1] * e[2]
def emit(pid, fam, boxesA, boxB, extra=""):
    v, t = [], []
    for lo, hi in boxesA: v, t = append((v, t), box(list(lo), list(hi)))
    bv, bt = box(list(boxB[0]), list(boxB[1]))
    VA = sum(vbox(b) for b in boxesA); VB = vbox(boxB); assert VA == vol6(v, t) and VB == vol6(bv, bt)
    I = sum((ov(b, boxB) for b in boxesA), Fraction(0)); U = VA + VB - I; D = VA - I
    write_msh(os.path.join(out, "adversarial", pid + "_A.msh"), v, t); write_msh(os.path.join(out, "adversarial", pid + "_B.msh"), bv, bt)
    man.append("\t".join([pid, "control", f"valid,control,{fam}" + extra, str(len(v) // 3), str(len(t) // 3), "8", "12", dyadic(VA), dyadic(VB), dyadic(U), dyadic(I), dyadic(D)]))
def variants(pid, fam, boxesA, k):
    for name, fr in VARIANTS.items():
        blo = [sc(0.0 + fr[d][0] * 4.0, k) for d in range(3)]; bhi = [sc(0.0 + fr[d][1] * 4.0, k) for d in range(3)]   # fractions of [0,4]^3 (exact in doubles for the dyadic scale)
        emit(f"{pid}_{name}", fam, boxesA, (tuple(blo), tuple(bhi)), "," + name)
def mk(lo, hi, k): return (tuple(sc(x, k) for x in lo), tuple(sc(x, k) for x in hi))
n = 0
for name, lo, hi in (("sub", (l, 1.0, 1.0), (2.0, 2.0, 2.0)), ("ulp", (1.0, 1.0, 1.0), (u, u, u))):
    variants(f"ctl_S1_{name}", "S1", [mk(lo, hi, 0)], 0)
for k in (0, 30):
    for a in range(3):
        for tname, t in (("l", l), ("p51", ulp2), ("p100", 2.0 ** -100)):
            lo, hi = [0.0] * 3, [4.0] * 3; hi[a] = t
            variants(f"ctl_S2_near{tname}_ax{a}_k{k}", "S2", [mk(lo, hi, k)], k)
        lo, hi = [0.0] * 3, [4.0] * 3; lo[a] = u
        variants(f"ctl_S2_farp51_ax{a}_k{k}", "S2", [mk(lo, hi, k)], k)
for a in range(3):
    for gname, (h1, l2) in (("l", (0.0, l)), ("p51", (2.0, math.nextafter(2.0, 5.0)))):
        b1l, b1h, b2l, b2h = [0.0] * 3, [4.0] * 3, [0.0] * 3, [4.0] * 3
        if gname == "l": b1l[a], b1h[a], b2l[a], b2h[a] = -2.0, 0.0, l, 2.0
        else: b1l[a], b1h[a], b2l[a], b2h[a] = 0.0, 2.0, l2, 4.0
        variants(f"ctl_S3_gap{gname}_ax{a}", "S3", [mk(b1l, b1h, 0), mk(b2l, b2h, 0)], 0)
for a in range(3):
    alo, ahi, blo, bhi = [0.0] * 3, [4.0] * 3, [1.0] * 3, [3.0] * 3
    blo[a], bhi[a] = u, 6.0
    emit(f"ctl_S4_ulpoverlap_ax{a}", "S4", [mk(alo, ahi, 0)], mk(blo, bhi, 0), ",overlap")
    alo, ahi, blo, bhi = [0.0] * 3, [4.0] * 3, [1.0] * 3, [3.0] * 3
    alo[a], blo[a], bhi[a] = l, -2.0, 2 * l
    emit(f"ctl_S4_loverlap_ax{a}", "S4", [mk(alo, ahi, 0)], mk(blo, bhi, 0), ",overlap")
open(os.path.join(out, "manifest.tsv"), "w").write("\n".join(man) + "\n")
print(f"make_control_pairs: {len(man) - 1} pairs")
