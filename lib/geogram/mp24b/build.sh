#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Build geogram_fidelity2 (and the five planted mutants) against a built MP22-state libgeogram.a (GEOGRAM_DIR = the tree: <dir>/src, <dir>/build).
# usage: GEOGRAM_DIR=<tree> BOOST=<boost root> [OUTDIR=<dir>] [MUTANTS=1] [EXTRA="-DT_EXP=n"] sh build.sh
# Adapted from the frozen research-history script only in its path handling (see docs/PROVENANCE.md); the compiler flags are unchanged.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
G="${GEOGRAM_DIR:?set GEOGRAM_DIR}"
OUT="${OUTDIR:-$G/bin}"; mkdir -p "$OUT"
export TMPDIR="${TMPDIR:-$ROOT/build/tmp}"; mkdir -p "$TMPDIR"
B="${BOOST:-${EXACTMESH_DEPS:-$ROOT/build/deps}/boost_1_88_0}"; INC="-I$B"
OMP=""; [ "$(uname -s)" = Linux ] && OMP="-fopenmp"
build() {  # name defines
  ${CXX:-c++} -std=c++17 -O2 -frounding-math -ffp-contract=off -DNDEBUG $2 ${EXTRA:-} $INC -I"$G/src/src/lib" -I"$G/build/src/lib" \
    "$HERE/geogram_fidelity2.cpp" "$G/build/lib/libgeogram.a" $OMP -lpthread -ldl -o "$OUT/$1"
  echo "built $OUT/$1"
}
build geogram_fidelity2 ""
if [ "${MUTANTS:-0}" = 1 ]; then for m in NOSCALE SCALE19 C0 NOBACK TLARGE; do build geogram_fidelity2_mut_$m -DMUT_$m; done; fi
