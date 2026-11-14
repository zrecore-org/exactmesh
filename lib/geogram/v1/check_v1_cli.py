#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V2 bar (b) (docs/PLAN.md, "V2"): the V1 corpus and the three former known-limit inputs through the pipeline CLI geogram_fidelity2.
usage: python3 -I check_v1_cli.py <geogram_fidelity2> <v1 set dir (gen_v1 output)> <scratch dir> [jobs]
Each case is run as operand A against a unit cube (all three operations, default mode and --no-cavities) and as operand B (union, both modes). Exit status 0 iff every expectation holds."""
import os, subprocess, sys, re
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from meshpy import *

cli, setdir, scratch = sys.argv[1:4]; jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 6
os.makedirs(scratch, exist_ok=True)
OPS = ["union", "intersection", "difference"]

def run(args, tag):
    out = os.path.join(scratch, tag + ".out.msh")
    p = subprocess.run([cli] + args + [out], capture_output=True, text=True, timeout=120, env=dict(os.environ, GEOGRAM_RUNNER_THREADS="1"))
    return p.returncode, p.stderr

def reason(err, side):
    m = re.search(r"REFUSED: invalid input %s: (\S+?)(?::| |$)" % side, err)
    return m.group(1) if m else None

fails = []; counts = {"runs": 0}
def expect(tag, cond, msg):
    counts["runs"] += 1
    if not cond: fails.append(f"{tag}: {msg}")

def unit_cube_beside(v):
    hi = [max(v[k::3]) for k in range(3)]; lo = [min(v[k::3]) for k in range(3)]
    return box([hi[0] + 1, lo[1], lo[2]], [hi[0] + 2, lo[1] + 1, lo[2] + 1])

def check(tag, mesh, exp_default, exp_strict, legit_cavities=None):
    """mesh: (v, t); exp_default / exp_strict: 'ACCEPT' or a V1 code (or 'OLD')"""
    cube = unit_cube_beside(mesh[0]); mp = os.path.join(scratch, tag + "_M.msh"); cp = os.path.join(scratch, tag + "_C.msh")
    write_msh(mp, *mesh); write_msh(cp, *cube)
    res = []
    for mode, exp in (("default", exp_default), ("strict", exp_strict)):
        flag = ["--no-cavities"] if mode == "strict" else []
        for op in OPS:
            rc, err = run(flag + ["boolean", op, mp, cp], f"{tag}_{mode}_{op}_A")
            t = f"{tag} {mode} {op} as A"
            if exp == "ACCEPT": expect(t, rc == 0 or (rc == 3 and "REFUSED: hard limit" in err), f"expected accepted, exit {rc}: {err.strip()[:120]}")
            else: expect(t, rc == 3 and reason(err, "A") == exp, f"expected exit 3 and {exp}, got exit {rc}: {err.strip()[:120]}")
        rc, err = run(flag + ["boolean", "union", cp, mp], f"{tag}_{mode}_union_B")
        t = f"{tag} {mode} union as B"
        if exp == "ACCEPT": expect(t, rc == 0 or (rc == 3 and "REFUSED: hard limit" in err), f"expected accepted, exit {rc}: {err.strip()[:120]}")
        else: expect(t, rc == 3 and reason(err, "B") == exp, f"expected exit 3 and {exp}, got exit {rc}: {err.strip()[:120]}")

# (i) the three former known-limit inputs
outer = box((0, 0, 0), (4, 4, 4))
known = [("known_a_cavity", append(outer, box((1, 1, 1), (2, 2, 2), True)), "ACCEPT", "V1-CAVITY-NOT-ALLOWED"),
         ("known_b_same_orientation_nested", append(outer, box((1, 1, 1), (2, 2, 2))), "V1-NESTED-SAME-ORIENTATION", "V1-NESTED-SAME-ORIENTATION"),
         ("known_c_negative_beside", append(outer, box((10, 0, 0), (11, 1, 1), True)), "V1-NEGATIVE-OUTSIDE", "V1-NEGATIVE-OUTSIDE")]
for tag, m, d, s in known: assert vol6(*m) != 0

rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(setdir, "manifest.tsv"))][1:]
tasks = [(tag, m, d, s) for tag, m, d, s in known]
for r in rows:
    cid, cls, fam, ed, es = r[:5]
    tasks.append((cid, read_msh(os.path.join(setdir, "cases", cid + ".msh")), ed, es))
with ThreadPoolExecutor(jobs) as ex: list(ex.map(lambda a: check(*a), tasks))
nleg = sum(1 for r in rows if r[1] == "legit"); ninv = sum(1 for r in rows if r[1] == "invalid")
print(f"check_v1_cli: {len(known)} known-limit inputs + {nleg} legit + {ninv} invalid cases; {counts['runs']} CLI runs (A in 3 operations and B in union, default and --no-cavities); {len(fails)} mismatches")
for f in fails[:60]: print("MISMATCH", f)
sys.exit(1 if fails else 0)
