#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""Summary of the V2 bar (c) scoring: usage summarize_cavity_pairs.py <work dir>. Exit 0 iff 0 FAIL and 0 REFUSED other than documented hard limits."""
import csv, glob, os, re, sys
from collections import Counter
W = sys.argv[1]
man = {r[0]: r for r in list(csv.reader(open(os.path.join(W, "pairs/manifest.tsv")), delimiter="\t"))[1:]}
rows = []
for f in sorted(glob.glob(os.path.join(W, "out_*/results.tsv"))): rows += list(csv.reader(open(f), delimiter="\t"))[1:]
print(f"cavity pairs scored: {len(rows)} rows ({len(man)} pairs x 3 operations = {3 * len(man)} expected)")
bad = []; c = Counter(); cp = Counter(); exactrows = Counter()
for cid, op, kind, verdict, sec, sha, detail, fid in rows:
    point = "pointtest" in man[cid][2]; variant = man[cid][2].split(",")[2]
    vol = (re.search(r"volume=(\w+)", detail) or [None, "-"])[1]
    c[("point-test-only" if point else "exact-volume", verdict)] += 1; cp[(variant, verdict)] += 1
    if not point and verdict == "PASS": exactrows[vol] += 1
    if verdict == "REFUSED" and "REFUSED: hard limit" in detail: bad.append(("hard-limit refusal", cid, op, detail[:140])); continue
    if verdict != "PASS": bad.append((verdict, cid, op, detail[:200]))
for (k, v), n in sorted(c.items()): print(f"  {k:16s} {v:10s} {n}")
print("  by variant:", dict(sorted(cp.items())))
print("  PASS rows with exact volume expectation, scorer volume status:", dict(exactrows))
hl = [b for b in bad if b[0] == "hard-limit refusal"]; other = [b for b in bad if b[0] != "hard-limit refusal"]
print(f"  hard-limit refusals (acceptable, listed): {len(hl)}")
for b in hl: print("    ", *b)
print(f"  FAIL / other non-PASS rows: {len(other)}")
for b in other: print("    ", *b)
sys.exit(0 if len(rows) == 3 * len(man) and not other else 1)
