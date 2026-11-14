#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V2 bar (c) (docs/PLAN.md, "V2"): pairs of a V1 'legit' (cavity) operand A with an axis-aligned box B, with exact expected volumes where the harness can have them.
usage: python3 -I make_cavity_pairs.py <v1 set dir> <out dir>      writes <out>/manifest.tsv and <out>/adversarial/<id>_{A,B}.msh in the layout of oracle/compare.cpp / score_fidelity2.py.
Variants of B (fractions of A's bounding box, rounded to doubles; no random numbers): contain, disjoint, cutA, cutB. Expectations (all exact Fractions of the doubles, never a subtraction of doubles):
  contain : union = V(B), intersection = V(A), difference = 0.   disjoint: union = V(A) + V(B), intersection = 0, difference = V(A).
  cut*    : if every component of A is an axis-aligned box: V(A n B) = sum_i sign_i V(box_i n B) (alternating-sign nesting: solid +, cavity -, island +), union = V(A) + V(B) - V(A n B),
            difference = V(A) - V(A n B); otherwise the three volume columns are '-' and the scorer judges by exact point-in-solid classification only.
Volumes are 6 x volume (the convention of exactVolumeTimes6)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fractions import Fraction
from meshpy import *

setdir, out = sys.argv[1:3]
os.makedirs(os.path.join(out, "adversarial"), exist_ok=True)
VARIANTS = {   # per axis (lo_frac, hi_frac): B spans [lo + lo_frac*w, lo + hi_frac*w], w = extent of A along that axis
    "contain": [(-0.5, 1.5)] * 3,
    "disjoint": [(1.5, 2.5), (0.25, 0.75), (0.25, 0.75)],
    "cutA": [(0.37, 1.30), (-0.2, 0.61), (0.13, 1.40)],
    "cutB": [(0.113, 0.707), (0.051, 1.109), (-0.131, 0.497)],
}
def axis_box(v, t, tris):
    idx = sorted({t[3 * k + j] for k in tris for j in range(3)})
    if len(idx) != 8 or len(tris) != 12: return None
    ax = [sorted({v[3 * i + d] for i in idx}) for d in range(3)]
    return ax if all(len(a) == 2 for a in ax) else None

rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(setdir, "manifest.tsv"))][1:]
man = ["id\tclass\ttags\tnvA\tntA\tnvB\tntB\tvol6_A\tvol6_B\tvol6_union\tvol6_inter\tvol6_diff"]
nexact = npoint = ncoplanar = 0
for r in rows:
    cid, cls = r[0], r[1]
    if cls != "legit": continue
    v, t = read_msh(os.path.join(setdir, "cases", cid + ".msh"))
    lo = [min(v[d::3]) for d in range(3)]; hi = [max(v[d::3]) for d in range(3)]; w = [hi[d] - lo[d] for d in range(3)]
    comps = components(v, t); VA = vol6(v, t)
    boxes = [axis_box(v, t, c) for c in comps]; allbox = all(b is not None for b in boxes)
    signs = [1 if vol6(v, t, c) > 0 else -1 for c in comps]
    coords = [{x for b in boxes if b for x in b[d]} for d in range(3)]
    for name, fr in VARIANTS.items():
        blo = [lo[d] + fr[d][0] * w[d] for d in range(3)]; bhi = [lo[d] + fr[d][1] * w[d] for d in range(3)]
        bv, bt = box(blo, bhi); VB = vol6(bv, bt)
        if any(blo[d] in coords[d] or bhi[d] in coords[d] for d in range(3)): ncoplanar += 1
        if name == "contain": U, I, D = VB, VA, Fraction(0)
        elif name == "disjoint": U, I, D = VA + VB, Fraction(0), VA
        elif allbox:
            I = Fraction(0)
            for b, s in zip(boxes, signs):
                e = [max(Fraction(0), min(Fraction(b[d][1]), Fraction(bhi[d])) - max(Fraction(b[d][0]), Fraction(blo[d]))) for d in range(3)]
                I += s * 6 * e[0] * e[1] * e[2]
            U, D = VA + VB - I, VA - I
        else: U = I = D = None
        pid = f"{cid}_{name}"
        write_msh(os.path.join(out, "adversarial", pid + "_A.msh"), v, t); write_msh(os.path.join(out, "adversarial", pid + "_B.msh"), bv, bt)
        cols = [dyadic(x) if x is not None else "-" for x in (U, I, D)]
        if U is None: npoint += 1
        else: nexact += 1
        man.append("\t".join([pid, "cavity", "valid,cavity," + name + ("" if U is not None else ",pointtest"), str(len(v) // 3), str(len(t) // 3), "8", "12", dyadic(VA), dyadic(VB)] + cols))
open(os.path.join(out, "manifest.tsv"), "w").write("\n".join(man) + "\n")
print(f"make_cavity_pairs: {len(man) - 1} pairs ({nexact} with exact volume expectations, {npoint} judged by point tests only), {ncoplanar} pairs where a face of B is coplanar with a component face")
