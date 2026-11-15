#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V4 (docs/PLAN.md, "V4"), bar (a): re-score every recorded output mesh with an OLD and a NEW mesh_compare and list every row whose verdict differs.
usage: rescore_all.py <old mesh_compare> <new mesh_compare> <set-name> <corpus dir> <results dir> [<results dir> ...]   (run with python3 -I)
A results dir holds results.tsv and <case>.<op>.msh as written by score_library.py / score_fidelity2.py. Rows without an output file (REFUSED, TIMEOUT, ...) are not scored by mesh_compare and are counted only.
Prints per set: counts (recorded verdict, old rescored, new rescored); every row whose NEW verdict differs from the RECORDED one or from the OLD rescored one, with both detail lines; and the number of rows whose
verdict is unchanged but whose detail line differs (informational). Writes <set-name>.rows.tsv (case, op, recorded, old, new, old detail, new detail) next to the first results dir's parent if --out is given."""
import csv, os, subprocess, sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
args = sys.argv[1:]
out = None
if "--out" in args: i = args.index("--out"); out = args[i + 1]; del args[i:i + 2]
old, new, name, corpus = args[:4]; dirs = args[4:]
def verdict(det):
    return "PASS" if " PASS " in det else "ACCEPTED-UNVERIFIED" if "ACCEPTED-UNVERIFIED" in det else "FAIL"
def run(binary, cid, op, path):
    q = subprocess.run([binary, "score", corpus, cid, op, path, "5000"], capture_output=True, text=True)
    det = q.stdout.strip().splitlines()[-1] if q.stdout.strip() else q.stderr.strip()
    return verdict(det), det
rows = []; skipped = Counter()
for d in dirs:
    for r in list(csv.reader(open(os.path.join(d, "results.tsv")), delimiter="\t"))[1:]:
        cid, op, kind, rec = r[0], r[1], r[2], r[3]; path = os.path.join(d, f"{cid}.{op}.msh")
        if rec in ("REFUSED", "TIMEOUT", "CRASH", "ERROR") or not os.path.exists(path): skipped[rec] += 1; continue
        rows.append((cid, op, rec, path))
def work(x):
    cid, op, rec, path = x; vo, do = run(old, cid, op, path); vn, dn = run(new, cid, op, path); return (cid, op, rec, vo, vn, do, dn)
with ThreadPoolExecutor(max_workers=int(os.environ.get("JOBS", "8"))) as ex: res = list(ex.map(work, rows))
tot = Counter((r[2], r[3], r[4]) for r in res)
print(f"== {name}: {len(res)} rows scored by both binaries, not re-scored (no output): {dict(skipped)}")
for k, v in sorted(tot.items()): print(f"  recorded {k[0]} -> old {k[1]} -> new {k[2]}: {v}")
bad_old = [r for r in res if r[2] != r[3]]
print(f"  rows where the OLD re-score differs from the recorded verdict (should be 0): {len(bad_old)}")
chg = [r for r in res if r[4] != r[3] or r[4] != r[2]]
print(f"  rows whose NEW verdict differs from the old/recorded one: {len(chg)}")
for r in chg: print(f"    {r[0]} {r[1]}: recorded {r[2]}, old {r[3]}, new {r[4]}\n      old: {r[5][:300]}\n      new: {r[6][:300]}")
dd = [r for r in res if r[3] == r[4] and r[5] != r[6]]
print(f"  rows with the same verdict but a different detail line (informational): {len(dd)}")
for r in dd[:12]: print(f"    {r[0]} {r[1]}: old: {r[5][:200]}\n      new: {r[6][:200]}")
if out:
    with open(out, "w") as f:
        w = csv.writer(f, delimiter="\t"); w.writerows(res)
