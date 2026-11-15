#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V3 (docs/PLAN.md, "V3"), part (a): exact analysis of one row of a pair directory through the instrumented CLI copy.
usage: v3_analyze.py <geogram_fidelity2_v3> <pair dir> <case id> <union|intersection|difference> <work dir> [--nosimplify] [--baseline-sha <sha256_16>] [--points N]
Runs the instrumented CLI (V3_DUMP), then, with exact rationals only:
  * the gate: sha256_16 of the output mesh against the baseline (the V2 CLI's recorded value);
  * exact 6 x volume of (E) the rational-vertex mesh Geogram holds before `approximate`, (G) Geogram's rounded mesh (= the CLI output), (C) the same exact points correctly rounded (float(Fraction)), each against the manifest expectation (frame s=0, c=0 only: checked from the CLI line);
  * zero-area, flipped (normal opposite to the exact one) and off-plane triangles (no axis on which the 3 vertices share one value) in E, G, C;
  * number of vertices whose rounded value differs from the exact point and the largest error in ulps, for G and C;
  * the scorer's sample loop (mt19937_64(12345), same arithmetic), the points classified differently from the boolean of A and B in E, G, C (exact point-in-solid with the same symbolic tie-break as harness/pip.h).
Run with python3 -I."""
import hashlib, math, os, re, struct, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../v1"))
from fractions import Fraction as F
from meshpy import read_msh, vol6

def hx(s): return F(float.fromhex(s))
def parse_dump(path):
    V = []; Vr = []; kind = []; T = []
    for line in open(path):
        w = line.split()
        if w[0] in ("E", "I"):
            Vr.append(tuple(float.fromhex(x) for x in w[2:5])); kind.append(w[0])
            if w[0] == "I": V.append(tuple(F(x) for x in Vr[-1]))
            else:
                rest = " ".join(w[5:]).split("|")[1:]; comps = []
                for r in rest:
                    t = r.split(); n = int(t[0]); comps.append(sum((hx(x) for x in t[1:1 + n]), F(0)))
                W, X, Y, Z = comps; assert W != 0
                V.append((X / W, Y / W, Z / W))
        elif w[0] == "T":
            idx = [int(x) for x in w[1:]]
            for k in range(1, len(idx) - 1): T.append((idx[0], idx[k], idx[k + 1]))
    return V, Vr, kind, T
def parse_dy(s):
    a, b = s.split("*2^"); return F(int(a)) * F(2) ** int(b)
def ff(x, fmt=".3g"):
    """float formatting of a Fraction that may overflow a double"""
    try: return format(float(x), fmt)
    except OverflowError: return "~2^%d" % (x.numerator.bit_length() - x.denominator.bit_length())
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def sg(x): return (x > 0) - (x < 0)
def vol6t(V, T):
    s = F(0)
    for a, b, c in T: s += dot(V[a], cross(V[b], V[c]))
    return s
def ulp(x):
    x = abs(x)
    if x == 0: return 5e-324
    return math.ulp(x)
def mesh_stats(V, T, ref):   # ref = exact mesh (for flip test)
    zero = flip = off = 0
    for (a, b, c) in T:
        n = cross(sub(V[b], V[a]), sub(V[c], V[a]))
        if n == (0, 0, 0): zero += 1
        nr = cross(sub(ref[b], ref[a]), sub(ref[c], ref[a]))
        if dot(n, nr) < 0: flip += 1
        if not any(V[a][d] == V[b][d] == V[c][d] for d in range(3)): off += 1
    return zero, flip, off
def tri_tables(V, T):
    return [(V[a], V[b], V[c], min(V[a][1], V[b][1], V[c][1]), max(V[a][1], V[b][1], V[c][1]), min(V[a][2], V[b][2], V[c][2]), max(V[a][2], V[b][2], V[c][2])) for a, b, c in T]
def orient_yz(u, v, p):
    s = sg((u[1] - p[1]) * (v[2] - p[2]) - (u[2] - p[2]) * (v[1] - p[1]))
    if s != 0: return s
    s = sg(u[2] - v[2])
    return s if s != 0 else sg(v[1] - u[1])
def classify(tab, p):
    """(inside, boundary) by +x ray parity with p' = (py + e, pz + e^2); same rules as harness/pip.h"""
    cr = 0
    for a, b, c, y0, y1, z0, z1 in tab:
        if p[1] < y0 or p[1] > y1 or p[2] < z0 or p[2] > z1: continue
        n = cross(sub(b, a), sub(c, a))
        if n != (0, 0, 0) and dot(n, sub(p, a)) == 0:
            if dot(cross(sub(b, a), sub(p, a)), n) >= 0 and dot(cross(sub(c, b), sub(p, b)), n) >= 0 and dot(cross(sub(a, c), sub(p, c)), n) >= 0: return False, True
        s0, s1, s2 = orient_yz(a, b, p), orient_yz(b, c, p), orient_yz(c, a, p)
        if s0 == 0 or s1 == 0 or s2 == 0 or not (s0 == s1 == s2): continue
        if n[0] == 0: continue
        d = (a[0] - (n[1] * (p[1] - a[1]) + n[2] * (p[2] - a[2])) / n[0]) - p[0]
        if d == 0:
            d = -n[1] / n[0]
            if d == 0: d = -n[2] / n[0]
        if d > 0: cr += 1
    return bool(cr & 1), False
class MT64:
    def __init__(s, seed):
        s.mt = [0] * 312; s.mt[0] = seed
        for i in range(1, 312): s.mt[i] = (6364136223846793005 * (s.mt[i - 1] ^ (s.mt[i - 1] >> 62)) + i) & (2**64 - 1)
        s.i = 312
    def __call__(s):
        if s.i >= 312:
            for k in range(312):
                x = (s.mt[k] & 0xFFFFFFFF80000000) | (s.mt[(k + 1) % 312] & 0x7FFFFFFF)
                s.mt[k] = s.mt[(k + 156) % 312] ^ (x >> 1) ^ (0xB5026F5AA96619E9 if x & 1 else 0)
            s.i = 0
        x = s.mt[s.i]; s.i += 1
        x ^= (x >> 29) & 0x5555555555555555; x ^= (x << 17) & 0x71D67FFFEDA60000; x ^= (x << 37) & 0xFFF7EEE000000000; x ^= x >> 43
        return x & (2**64 - 1)
def sample_points(A, B, npts):   # A, B: (v, t) double meshes; replicates oracle/compare.cpp
    lo = [1e300] * 3; hi = [-1e300] * 3
    for v, _ in (A, B):
        for i in range(0, len(v), 3):
            for k in range(3): lo[k] = min(lo[k], v[i + k]); hi[k] = max(hi[k], v[i + k])
    mx = 1.0
    for k in range(3): mx = max(mx, max(abs(lo[k]), abs(hi[k])))
    g = max(2.0 ** -14, 2.0 ** (math.ceil(math.log2(mx)) - 50)); rng = MT64(12345); pts = []
    for _ in range(npts):
        p = []
        for k in range(3):
            span = hi[k] - lo[k]; v = lo[k] - 0.25 * span + (float(rng() >> 11) / 9007199254740992.0) * 1.5 * span
            p.append((math.floor(v / g) + 0.5) * g)
        pts.append(tuple(F(x) for x in p))
    return pts
def inop(op, a, b): return (a or b) if op == "union" else (a and b) if op == "intersection" else (a and not b)

def main():
    a = sys.argv[1:]
    cli, pdir, cid, op, work = a[:5]
    nosimp = "--nosimplify" in a; npts = int(a[a.index("--points") + 1]) if "--points" in a else 5000
    base = a[a.index("--baseline-sha") + 1] if "--baseline-sha" in a else None
    os.makedirs(work, exist_ok=True)
    tag = f"{cid}.{op}" + (".nosimp" if nosimp else ""); out = os.path.join(work, tag + ".msh"); dump = os.path.join(work, tag + ".dump")
    for f in (out, dump):
        if os.path.exists(f): os.remove(f)
    env = dict(os.environ, V3_DUMP=dump, GEOGRAM_RUNNER_THREADS="1")
    if nosimp: env["V3_NOSIMPLIFY"] = "1"
    p = subprocess.run([cli, "boolean", op, f"{pdir}/adversarial/{cid}_A.msh", f"{pdir}/adversarial/{cid}_B.msh", out], capture_output=True, text=True, env=env, timeout=120)
    err = p.stderr.strip().splitlines(); line = next((l for l in err if l.startswith("fidelity2:")), ""); stage = next((l for l in err if l.startswith("v3:")), "")
    print(f"== {tag}: exit {p.returncode} | {stage} | {line[:110]}")
    if p.returncode != 0: print("   ", next((l for l in err if l.startswith("REFUSED")), err[-1:] )); return
    m = re.search(r"path=(\w+) s=(\d+).* c=\(([^,]+),([^,]+),([^)]+)\)", line)
    sc = int(m.group(2)); cvec = [float(m.group(3 + k)) for k in range(3)]; ident = sc == 0 and cvec == [0.0, 0.0, 0.0]
    print(f"   frame: path={m.group(1)} s={sc} c={cvec} ({'identity back-transform: the pipeline rounding is a no-op' if ident else 'back-transform ACTIVE: exact values below are mapped to the original frame as (x + c) * 2^s'})")
    sha = hashlib.sha256(open(out, "rb").read()).hexdigest()[:16]
    if base: print(f"   gate: sha256_16 {sha} vs baseline {base}: {'REPRODUCED' if sha == base else 'MISMATCH (do not use this dump)'}")
    V, Vr, kind, T = parse_dump(dump)
    ov, ot = read_msh(out)
    def back(P): return tuple((P[d] + F(cvec[d])) * F(2) ** sc for d in range(3))
    Eo = [back(p) for p in V]                                   # exact, original frame
    Gx = [back(tuple(F(x) for x in r)) for r in Vr]             # Geogram's rounded doubles, exact back-transform (Geogram rounding only)
    G = [tuple(F(x) for x in ov[3 * i:3 * i + 3]) for i in range(len(V))]   # the CLI output (Geogram rounding + translate-back rounding)
    assert len(ov) == 3 * len(V) and all(F(float(Gx[i][d])) == G[i][d] for i in range(len(V)) for d in range(3)), "CLI output is not fl(Gx): back-transform not reproduced"
    C = [tuple(F(float(c)) for c in Eo[i]) for i in range(len(V))]
    man = next(r for r in (l.rstrip("\n").split("\t") for l in open(os.path.join(pdir, "manifest.tsv"))) if r[0] == cid)
    exp = {"union": man[9], "intersection": man[10], "difference": man[11]}[op]
    ex = parse_dy(exp) if exp != "-" else None
    nE = sum(k == "E" for k in kind); print(f"   vertices {len(V)} ({nE} constructed, exact expansions), triangles {len(T)}")
    V = Eo
    for name, M in (("E exact (pre-rounding)", V), ("Gx geogram-rounded only", Gx), ("G final CLI output", G), ("C correctly-rounded E", C)):
        vv = vol6t(M, T); z, fl, off = mesh_stats(M, T, V)
        nd = sum(M[i] != V[i] for i in range(len(V)))
        mx = max((abs(M[i][d] - V[i][d]) / F(ulp(max(abs(float(V[i][d])), abs(float(M[i][d]))))) for i in range(len(V)) for d in range(3)), default=0)
        dv = (vv - ex) if ex is not None else None
        rel = (dv / ex) if (dv is not None and ex) else None
        print(f"   {name:24s} vol6 - expected = {ff(dv) if dv is not None else 'n/a':>10} (exactly 0: {dv == 0 if dv is not None else 'n/a'}; relative {ff(rel) if rel is not None else 'n/a'}; in units of 2^-1074: {ff(dv * F(2) ** 1074) if dv is not None else 'n/a'}) | zero-area {z} flipped {fl} off-plane {off} | vertices != exact {nd}, max err {ff(mx)} ulp")
    if "--no-points" in a: return
    Av, At = read_msh(f"{pdir}/adversarial/{cid}_A.msh"); Bv, Bt = read_msh(f"{pdir}/adversarial/{cid}_B.msh")
    def ft(v, t): return tri_tables([tuple(F(x) for x in v[i:i + 3]) for i in range(0, len(v), 3)], [tuple(t[i:i + 3]) for i in range(0, len(t), 3)])
    tA, tB = ft(Av, At), ft(Bv, Bt); tabs = {"E": tri_tables(V, T), "G": tri_tables(G, T), "C": tri_tables(C, T)}   # G = final CLI output
    bad = {k: [] for k in tabs}; skip = 0; pts = sample_points((Av, At), (Bv, Bt), npts)
    for p in pts:
        ia, ba = classify(tA, p); ib, bb = classify(tB, p)
        res = {k: classify(t, p) for k, t in tabs.items()}
        if ba or bb or any(r[1] for r in res.values()): skip += 1; continue
        e = inop(op, ia, ib)
        for k, r in res.items():
            if r[0] != e: bad[k].append(p)
    print(f"   sample points {npts} (skipped on a surface: {skip}); classified differently from the boolean of A and B: E {len(bad['E'])}, G {len(bad['G'])}, C {len(bad['C'])}")
    for p in bad["G"][:3]:
        print("     G-bad point", [float(x) for x in p])
        tab = tabs["G"]; cr = []
        for i, (a_, b_, c_, y0, y1, z0, z1) in enumerate(tab):
            if p[1] < y0 or p[1] > y1 or p[2] < z0 or p[2] > z1: continue
            s0, s1, s2 = orient_yz(a_, b_, p), orient_yz(b_, c_, p), orient_yz(c_, a_, p)
            if s0 == s1 == s2 != 0: cr.append((i, [[float(x) for x in v] for v in (a_, b_, c_)]))
        print("     G triangles whose (y,z) projection contains the point:", len(cr), "; first:", cr[:2])
if __name__ == "__main__": main()
