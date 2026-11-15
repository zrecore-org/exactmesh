#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# V3: build the instrumented CLI copy with the same flags as lib/geogram/mp24b/build.sh.  usage: GEOGRAM_DIR=<tree> BOOST=<boost root> OUTDIR=<dir> sh build_v3.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../../.." && pwd)"
G="${GEOGRAM_DIR:?set GEOGRAM_DIR}"; OUT="${OUTDIR:-$G/bin}"; mkdir -p "$OUT"
export TMPDIR="${TMPDIR:-$ROOT/build/tmp}"; mkdir -p "$TMPDIR"
B="${BOOST:-${EXACTMESH_DEPS:-$ROOT/build/deps}/boost_1_88_0}"; INC="-I$B"
OMP=""; [ "$(uname -s)" = Linux ] && OMP="-fopenmp"
${CXX:-c++} -std=c++17 -O2 -frounding-math -ffp-contract=off -DNDEBUG $INC -I"$G/src/src/lib" -I"$G/build/src/lib" \
  "$HERE/geogram_fidelity2_v3.cpp" "$G/build/lib/libgeogram.a" $OMP -lpthread -ldl -o "$OUT/geogram_fidelity2_v3"
echo "built $OUT/geogram_fidelity2_v3"
