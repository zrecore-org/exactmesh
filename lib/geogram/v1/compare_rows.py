#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V2 bar (a) (docs/PLAN.md, "V2"): per-row comparison of two score24b result directories (baseline saved before the change, and the re-run).
usage: python3 -I compare_rows.py <baseline score24b dir> <new score24b dir>
Compares case, op, kind, verdict, sha256_16 (hash of the output file) and detail (scorer text or refusal reason) of every row of g1, off, thin, set3, set5, f2; ignores seconds and the timing line.
Exit status 0 iff the row sets are equal and 0 rows differ."""
import csv, os, sys
a, b = sys.argv[1:3]; diff = 0; total = 0
for n in ("g1", "off", "thin", "set3", "set5", "f2"):
    ra = {(r[0], r[1]): (r[2], r[3], r[5], r[6]) for r in list(csv.reader(open(os.path.join(a, n + ".tsv")), delimiter="\t"))[1:]}
    rb = {(r[0], r[1]): (r[2], r[3], r[5], r[6]) for r in list(csv.reader(open(os.path.join(b, n + ".tsv")), delimiter="\t"))[1:]}
    d = [k for k in set(ra) | set(rb) if ra.get(k) != rb.get(k)]
    print(f"{n}: {len(ra)} baseline rows, {len(rb)} new rows, {len(d)} differing")
    for k in sorted(d)[:20]: print("  DIFF", k, ra.get(k), rb.get(k))
    diff += len(d); total += len(ra)
print(f"compare_rows: {total} rows, {diff} differing")
sys.exit(1 if diff else 0)
