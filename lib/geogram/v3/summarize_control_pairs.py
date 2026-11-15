#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V3 part (b): summary of the control scoring: usage summarize_control_pairs.py <work dir>. Counts PASS / FAIL / REFUSED per family and lists every non-PASS row."""
import csv, glob, os, sys
from collections import Counter
W = sys.argv[1]
man = {r[0]: r for r in list(csv.reader(open(os.path.join(W, "pairs/manifest.tsv")), delimiter="\t"))[1:]}
rows = []
for f in sorted(glob.glob(os.path.join(W, "out_*/results.tsv"))): rows += list(csv.reader(open(f), delimiter="\t"))[1:]
print(f"control pairs: {len(man)}; rows scored {len(rows)} (expected {3 * len(man)})")
c = Counter(); tot = Counter()
for cid, op, kind, verdict, sec, sha, detail, fid in rows:
    fam = man[cid][2].split(",")[2]; c[(fam, verdict)] += 1; tot[verdict] += 1
for k, v in sorted(c.items()): print("  ", *k, v)
print("  total:", dict(tot))
for cid, op, kind, verdict, sec, sha, detail, fid in sorted(rows):
    if verdict != "PASS": print("  NON-PASS", cid, op, verdict, detail[:230], "|", fid[:90])
