#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Build geogram_runner against the permissive static libgeogram.a built by ci/geogram_xplat.sh build-chain.
# usage: GEOGRAM_DIR=<tree: <dir>/src, <dir>/build> sh lib/geogram/build.sh
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"                          # repo root
G="${GEOGRAM_DIR:?set GEOGRAM_DIR to the Geogram tree built by ci/geogram_xplat.sh build-chain}"
export TMPDIR="${TMPDIR:-$ROOT/build/tmp}"; mkdir -p "$TMPDIR"
BIN="$G/bin"; mkdir -p "$BIN"
# Same flags Geogram's own Darwin-clang platform uses for exact predicates: -frounding-math -ffp-contract=off
# On Linux/gcc libgeogram.a contains OpenMP code (nl_amgcl); the final link needs -fopenmp (macOS clang builds without it).
OMP=""; [ "$(uname -s)" = Linux ] && OMP="-fopenmp"
${CXX:-c++} -std=c++17 -O2 -frounding-math -ffp-contract=off -DNDEBUG \
  -I"$G/src/src/lib" -I"$G/build/src/lib" \
  "$HERE/runner.cpp" "$G/build/lib/libgeogram.a" $OMP -lpthread -ldl -o "$BIN/geogram_runner"
echo "built $BIN/geogram_runner"
