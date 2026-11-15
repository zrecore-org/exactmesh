# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#!/usr/bin/env python3
# V5 generator: the MP26 run_pairs.py (git show 69d256a1166:poc/gpu/mesh/lib/realmesh/run_pairs.py) reduced to "run the three tools on every pair and KEEP every output"; absolute A/B paths, no scoring, no deletion.
import json, os, signal, subprocess, sys, time
from multiprocessing import Pool
V = os.path.dirname(os.path.abspath(__file__))
TOOLS = {"v2": [V + "/bin/rec_fidelity2"], "geo": [V + "/bin/rec_geogram_runner"], "man": [V + "/bin/rec_manifold_runner"]}
OPS = ["union", "intersection", "difference"]; TIMEOUT = 600
def timed(cmd, env, errpath):
    t0 = time.monotonic()
    with open(errpath, "wb") as ef:
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=ef, env=env, start_new_session=True)
        while True:
            pid, st, ru = os.wait4(p.pid, os.WNOHANG)
            if pid: rc = os.waitstatus_to_exitcode(st); break
            if time.monotonic() - t0 > TIMEOUT:
                os.killpg(p.pid, signal.SIGKILL); os.wait4(p.pid, 0); rc = "TIMEOUT"; break
            time.sleep(0.02)
        p.returncode = 0
    return rc, time.monotonic() - t0
def do_pair(a):
    pid, A, B = a
    env = dict(os.environ, GEOGRAM_RUNNER_THREADS="1", OMP_NUM_THREADS="1", VECLIB_MAXIMUM_THREADS="1")
    rd = V + "/run/res"; ed = V + "/run/err"; recs = []
    for tool, base in TOOLS.items():
        for op in OPS:
            out = f"{rd}/{pid}.{tool}.{op}.msh"; err = f"{ed}/{pid}.{tool}.{op}.txt"
            if os.path.exists(out): os.remove(out)
            rc, wall = timed(base + ["boolean", op, A, B, out], env, err)
            recs.append(dict(pair=pid, tool=tool, op=op, rc=rc, wall_s=round(wall, 3), out_exists=os.path.exists(out)))
    open(f"{V}/run/gen/{pid}.json", "w").write(json.dumps(recs))
    return pid
if __name__ == "__main__":
    for d in ("res", "err", "gen"): os.makedirs(f"{V}/run/{d}", exist_ok=True)
    rows = [l.rstrip("\n").split("\t") for l in open(V + "/data/pairs.tsv")][1:]
    work = [(r[0], r[5], r[6]) for r in rows if not os.path.exists(f"{V}/run/gen/{r[0]}.json")]
    with Pool(6) as pool:
        for i, pid in enumerate(pool.imap_unordered(do_pair, work), 1): print(f"[{i}/{len(work)}] {pid}", flush=True)
