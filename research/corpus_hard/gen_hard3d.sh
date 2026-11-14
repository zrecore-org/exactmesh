#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# 3D corpus: builds gen_hard3d, generates twice (byte-identical or abort); --update writes EXPECTED_SHA256_3D.txt, --check compares.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ST=${RESEARCH_WORK:-$HERE/../../build/research}/hard2d3d
mkdir -p "$ST/bin"; c++ -std=c++17 -O2 -ffp-contract=off -Wall -o "$ST/bin/gen_hard3d" "$HERE/gen_hard3d.cpp"
rm -rf "$ST/cases3d" "$ST/gen3_again"; "$ST/bin/gen_hard3d" "$ST/cases3d"; "$ST/bin/gen_hard3d" "$ST/gen3_again"
SHA="shasum -a 256"; command -v sha256sum >/dev/null && SHA=sha256sum
h() { (cd "$1" && ls | LC_ALL=C sort | xargs $SHA); }
[ "$(h "$ST/cases3d")" == "$(h "$ST/gen3_again")" ] || { echo NOT reproducible; exit 1; }
echo reproducible
[ "$1" = "--update" ] && { h "$ST/cases3d" > "$HERE/EXPECTED_SHA256_3D.txt"; echo wrote EXPECTED_SHA256_3D.txt; }
[ "$1" = "--check" ] && { diff <(h "$ST/cases3d") "$HERE/EXPECTED_SHA256_3D.txt" && echo matches; }
rm -rf "$ST/gen3_again"
