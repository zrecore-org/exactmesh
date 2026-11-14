#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Builds gen_hard, generates the corpus twice (byte-identical or abort), --update writes EXPECTED_SHA256.txt, --check compares.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ST=${RESEARCH_WORK:-$HERE/../../build/research}/hard2d3d
mkdir -p "$ST/bin"; c++ -std=c++17 -O2 -ffp-contract=off -Wall -o "$ST/bin/gen_hard" "$HERE/gen_hard.cpp"
rm -rf "$ST/cases" "$ST/gen_again"; "$ST/bin/gen_hard" "$ST/cases"; "$ST/bin/gen_hard" "$ST/gen_again"
SHA="shasum -a 256"; command -v sha256sum >/dev/null && SHA=sha256sum
h() { (cd "$1" && ls | LC_ALL=C sort | xargs $SHA); }
[ "$(h "$ST/cases")" == "$(h "$ST/gen_again")" ] || { echo NOT reproducible; exit 1; }
echo reproducible
[ "$1" = "--update" ] && { h "$ST/cases" > "$HERE/EXPECTED_SHA256.txt"; echo wrote EXPECTED_SHA256.txt; }
[ "$1" = "--check" ] && { diff <(h "$ST/cases") "$HERE/EXPECTED_SHA256.txt" && echo matches; }
rm -rf "$ST/gen_again"
