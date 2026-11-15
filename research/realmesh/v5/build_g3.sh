#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# V5: builds the gap3d tools (same flags as research/lib/gap3d/build.sh) from the sources in <srcdir> into <bin>; Boost 1.92 / earcut / CDT / Manifold are the research checkout's recorded builds (read only).
# usage: build_g3.sh <srcdir> <bin> [extra c++ flags]
set -e
SRC=$1; BIN=$2; shift 2; X="$@"
G2=/Volumes/CrucialX9/Projects/mongo/storage/mesh/gap2d; M=/Volumes/CrucialX9/Projects/mongo/storage/mesh/manifold
BOOST=$G2/src/boost/boost_1_92_0; PRED=/Volumes/CrucialX9/Projects/exactmesh/build/deps/shewchuk/predicates.c
mkdir -p "$BIN"; CXX="c++ -std=c++17 -O2 -ffp-contract=off -DNDEBUG $X"
cc -O2 -ffp-contract=off -c -o "$BIN/predicates.o" "$PRED" -w
INC="-I$BOOST -I$G2/src/earcut/include -I$G2/src/cdt/CDT/include -I$M/src/include"
LIBS="$M/buildA/src/libmanifold.a $M/buildA/_deps/clipper2-build/libClipper2.a"
$CXX -DMANIFOLD_PAR=-1 $INC -o "$BIN/run3d" "$SRC/run3d.cpp" "$BIN/predicates.o" $LIBS
for m in 0 1 2 3 4 5 6 7; do $CXX -DMUTANT=$m -I"$BOOST" -o "$BIN/selftest3d_m$m" "$SRC/selftest3d.cpp" "$BIN/predicates.o"; done
$CXX -I"$BOOST" -o "$BIN/mink_origin" "$SRC/mink_origin.cpp" "$BIN/predicates.o"
