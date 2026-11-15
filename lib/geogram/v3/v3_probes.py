#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V3 part (c): probes for the Geogram exception `!point_outside_boundary` on legit_one_ulp_cavity_050/052 cutB (the same input pair).
usage: v3_probes.py <geogram_fidelity2_v3> <v2 cavity pairs dir> <scratch dir>
B is the unchanged cutB box of case 050 (and cutA for comparison). A is rebuilt with the generator's own constructions, changing one thing at a time:
  ladder   : cavity {x,1,1}..{2,2,2} inside [0,4]^3 with x in a ladder from the smallest subnormal up to 2^-10 (P1: subnormal vs normal; P3: thin but normal wall);
  boxonly  : the cavity solid alone as a positive box (P2: no cavity, no outer box) for the same x;
  axis     : the subnormal on the y or z axis instead of x (cavity and box-only);
  nocavity_outer: the outer box alone (sanity).
Prints, per probe and per B variant, the exit status, the failing stage and the V3 CLI line. Run with python3 -I."""
import math, os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../v1"))
from meshpy import *
cli, pairs, scratch = sys.argv[1:4]
os.makedirs(scratch, exist_ok=True)
l = math.nextafter(0.0, 1.0)
def run(tag, A, Bpath):
    av, at = A; ap = os.path.join(scratch, tag + "_A.msh"); write_msh(ap, av, at)
    r = []
    for op in ("union", "intersection", "difference"):
        p = subprocess.run([cli, "boolean", op, ap, Bpath, os.path.join(scratch, f"{tag}.{op}.msh")], capture_output=True, text=True, timeout=120, env=dict(os.environ, GEOGRAM_RUNNER_THREADS="1"))
        st = next((x for x in p.stderr.splitlines() if x.startswith("v3:")), "")
        ref = next((x for x in p.stderr.splitlines() if x.startswith("REFUSED")), "")
        r.append(f"{op[:5]}:{'ok' if p.returncode == 0 else 'EXC' if 'point_outside' in ref else 'exit%d' % p.returncode}({st[10:] if st else ''})" + ("" if 'point_outside' in ref or p.returncode == 0 else " " + ref[:70]))
    return " ".join(r)
LADDER = [("2^-1074 (l)", l), ("2^-1073", 2 * l), ("2^-1022", 2.0 ** -1022), ("2^-540", 2.0 ** -540), ("2^-300", 2.0 ** -300), ("2^-100", 2.0 ** -100), ("2^-52", 2.0 ** -52), ("2^-30", 2.0 ** -30), ("2^-10", 2.0 ** -10)]
for bv in ("cutB", "cutA"):
    Bp = os.path.join(pairs, "adversarial", f"legit_one_ulp_cavity_050_{bv}_B.msh")
    print(f"=== B = {bv} (unchanged)")
    outer = box([0, 0, 0], [4, 4, 4])
    print("outer box alone      ", run(f"outer_{bv}", outer, Bp))
    for name, x in LADDER:
        lo = [x, 1, 1]; hi = [2, 2, 2]
        print(f"cavity x_lo={name:12s}", run(f"cav_{x.hex()[:12]}_{bv}", append(outer, box(lo, hi, True)), Bp))
    for name, x in LADDER[:3] + LADDER[6:7]:
        print(f"boxonly x_lo={name:12s}", run(f"bo_{x.hex()[:12]}_{bv}", box([x, 1, 1], [2, 2, 2]), Bp))
    for ax in (1, 2):
        lo = [1, 1, 1]; lo[ax] = l
        print(f"cavity subnormal on axis {ax}", run(f"cavax{ax}_{bv}", append(outer, box(lo, [2, 2, 2], True)), Bp))
        print(f"boxonly subnormal on axis {ax}", run(f"boax{ax}_{bv}", box(lo, [2, 2, 2]), Bp))
