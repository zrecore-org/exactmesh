#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# V3: run v3_analyze.py on every row that is not PASS and not REFUSED in the given results directories (gate = the recorded sha256_16 of each row).
# usage: v3_batch.sh <geogram_fidelity2_v3> <pairs dir> <results dir containing out_*/results.tsv> <work dir> > report.txt
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; CLI="$1"; PD="$2"; RD="$3"; W="$4"
cat "$RD"/out_*/results.tsv | awk -F'\t' '$4=="FAIL" {print $1, $2, $6}' | sort | while read c o sha; do
  python3 -I "$HERE/v3_analyze.py" "$CLI" "$PD" "$c" "$o" "$W" --baseline-sha "$sha" 2>&1 | cut -c1-600
done
