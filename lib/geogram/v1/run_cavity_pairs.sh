#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# V2 bar (c): generate the cavity pairs (make_cavity_pairs.py) from a gen_v1 set and score them through the pipeline CLI with the existing scorer (score_fidelity2.py, mesh_compare).
# usage: lib/geogram/v1/run_cavity_pairs.sh <geogram_fidelity2> <gen_v1 set dir> <work dir> [jobs]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; CLI="$1"; SET="$2"; W="$3"; JOBS="${4:-6}"
rm -rf "$W"; mkdir -p "$W"
python3 -I "$HERE/make_cavity_pairs.py" "$SET" "$W/pairs"
ids=$(tail -n +2 "$W/pairs/manifest.tsv" | cut -f1)
i=0; for id in $ids; do echo "$id" >> "$W/ids_$((i % JOBS)).txt"; i=$((i + 1)); done
for k in $(seq 0 $((JOBS - 1))); do
  GEOGRAM_RUNNER_THREADS=1 python3 -I "$HERE/../mp24b/score_fidelity2.py" "cav$k" "$CLI" "$W/pairs" --points 5000 --outdir "$W/out_$k" --cases "$(paste -sd, "$W/ids_$k.txt")" > "$W/score_$k.log" 2>&1 &
done
wait
python3 -I "$HERE/summarize_cavity_pairs.py" "$W"
