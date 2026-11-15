#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V3: re-score recorded CLI outputs with a given mesh_compare binary (e.g. the exactinit copy) and list every row whose verdict differs from the recorded one.
usage: rescore.py <mesh_compare binary> <pairs dir> <results dir> [<results dir> ...]      (a results dir holds results.tsv and <case>.<op>.msh as written by score_fidelity2.py). Run with python3 -I."""
import csv, glob, os, subprocess, sys
from collections import Counter
cmp_, pairs = sys.argv[1:3]
tot = Counter(); diff = []
for d in sys.argv[3:]:
    for r in list(csv.reader(open(os.path.join(d, "results.tsv")), delimiter="\t"))[1:]:
        cid, op, kind, verdict, sec, sha, detail, fid = r
        if verdict in ("REFUSED", "TIMEOUT", "CRASH", "ERROR"): tot[("not re-scored", verdict)] += 1; continue
        out = os.path.join(d, f"{cid}.{op}.msh")
        if not os.path.exists(out): tot[("no output file", verdict)] += 1; continue
        q = subprocess.run([cmp_, "score", pairs, cid, op, out, "5000"], capture_output=True, text=True)
        det = q.stdout.strip().splitlines()[-1] if q.stdout.strip() else q.stderr.strip()
        nv = "PASS" if " PASS " in det else "ACCEPTED-UNVERIFIED" if "ACCEPTED-UNVERIFIED" in det else "FAIL"
        tot[(verdict, nv)] += 1
        if nv != verdict: diff.append((cid, op, verdict, nv, det[:260]))
for k, v in sorted(tot.items()): print("  recorded -> rescored:", k, v)
print("rows whose verdict changed:", len(diff))
for x in diff: print("  ", *x)
