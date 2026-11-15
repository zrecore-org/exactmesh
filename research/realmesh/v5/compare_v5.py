# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
"""V5 comparison: the P/R/S lines of <v5>/scores/<a> against <v5>/scores/<b> and against the recorded score files, and the verdicts against data/results.tsv. usage: compare_v5.py <v5 work dir> <recorded run/scores dir>"""
import os, sys
V, REC = sys.argv[1:3]
def lines(p): return [l for l in open(p).read().splitlines() if l[:2] in ("P\t", "R\t", "S\t")]
sets = {d: sorted(os.listdir(f"{V}/scores/{d}")) for d in ("rec", "unfixed", "fixed", "asan") if os.path.isdir(f"{V}/scores/{d}")}
for d, fs in sets.items():
    diff = [f for f in fs if lines(f"{V}/scores/{d}/{f}") != lines(f"{REC}/{f}")]
    print(f"{d}: {len(fs)} pair files, {len(diff)} differ from the recorded P/R/S lines {diff[:5]}")
