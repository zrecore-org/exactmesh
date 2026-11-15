#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# V3 part (b): generate the non-cavity control pairs and score them through the pipeline CLI with the existing scorer.
# usage: lib/geogram/v3/run_control_pairs.sh <geogram_fidelity2> <work dir> [jobs]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; CLI="$1"; W="$2"; JOBS="${3:-6}"
rm -rf "$W"; mkdir -p "$W"
python3 -I "$HERE/make_control_pairs.py" "$W/pairs"
ids=$(tail -n +2 "$W/pairs/manifest.tsv" | cut -f1)
i=0; for id in $ids; do echo "$id" >> "$W/ids_$((i % JOBS)).txt"; i=$((i + 1)); done
for k in $(seq 0 $((JOBS - 1))); do
  GEOGRAM_RUNNER_THREADS=1 python3 -I "$HERE/../mp24b/score_fidelity2.py" "ctl$k" "$CLI" "$W/pairs" --points 5000 --outdir "$W/out_$k" --cases "$(paste -sd, "$W/ids_$k.txt")" > "$W/score_$k.log" 2>&1 &
done
wait
python3 -I "$HERE/summarize_control_pairs.py" "$W"
