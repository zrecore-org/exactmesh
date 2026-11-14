#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# builds real_score into $RESEARCH_WORK/realmesh/bin (default build/research/realmesh/bin); needs third_party/fetch.sh boost shewchuk and a built libshewchuk_predicates.a (cmake build: build/libshewchuk_predicates.a)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
DEPS="${EXACTMESH_DEPS:-$ROOT/build/deps}"; RW="${RESEARCH_WORK:-$ROOT/build/research}/realmesh"
OUT="$RW/bin"; mkdir -p "$OUT"
${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$DEPS/boost_1_88_0" "$HERE/real_score.cpp" "${SHEWCHUK_LIB:-$ROOT/build/libshewchuk_predicates.a}" -o "$OUT/real_score"
echo "built $OUT/real_score"
