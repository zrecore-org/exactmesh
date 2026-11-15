# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#!/usr/bin/env python3
# V5 scorer driver: scores the kept outputs of one pair with the given real_score binary exactly as run_pairs.py did (argv: pair A B 5000 20260514 tool:op:path ...).  usage: score_v5.py <binname> <outdirname> [jobs]
import json, os, subprocess, sys, time
from multiprocessing import Pool
V = os.path.dirname(os.path.abspath(__file__)); BIN = sys.argv[1]; OUT = f"{V}/scores/{sys.argv[2]}"; JOBS = int(sys.argv[3]) if len(sys.argv) > 3 else 6
def one(r):
    pid, A, B = r; sc = f"{OUT}/{pid}.txt"
    if os.path.exists(sc): return pid
    recs = json.load(open(f"{V}/run/gen/{pid}.json")); res = []
    for x in recs:
        out = f"{V}/run/res/{pid}.{x['tool']}.{x['op']}.msh"
        if x["rc"] == 0 and os.path.exists(out): res.append(f"{x['tool']}:{x['op']}:{out}")
    t0 = time.monotonic()
    p = subprocess.run([BIN, "pair", A, B, "5000", "20260514"] + res, capture_output=True, timeout=7200)
    txt = p.stdout.decode(errors="replace") + "\n#rc=%s %.1fs\n" % (p.returncode, time.monotonic() - t0) + p.stderr.decode(errors="replace")
    open(sc + ".tmp", "w").write(txt); os.rename(sc + ".tmp", sc); return pid
if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    rows = [l.rstrip("\n").split("\t") for l in open(V + "/data/pairs.tsv")][1:]
    with Pool(JOBS) as pool:
        for i, pid in enumerate(pool.imap_unordered(one, [(r[0], r[5], r[6]) for r in rows]), 1):
            if i % 50 == 0: print(i, flush=True)
