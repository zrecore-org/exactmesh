#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Compares the aggregate hashes written by `ci/geogram_xplat.sh report24b` with ci/EXPECTED_AGGREGATES.txt.
# usage: ci/check_aggregates.sh <report24b dir> <platform: darwin-arm64 | linux>        exit 1 on a difference
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; R="$1"; P="$2"
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
want() { awk -v k="$1" '$1 == k {print $2}' "$HERE/EXPECTED_AGGREGATES.txt"; }
gotv="$(sha "$R/mp24b_verdicts.txt")"; goto="$(sha "$R/mp24b_output_sha256.txt")"; rc=0
echo "verdict list : $gotv"; echo "output list  : $goto"
[ "$gotv" = "$(want pipeline_verdicts_all_platforms)" ] && echo "verdict-list hash matches the recorded value" || { echo "::error title=verdict hash::differs from the recorded $(want pipeline_verdicts_all_platforms)"; rc=1; }
case "$P" in
  darwin-arm64) [ "$goto" = "$(want pipeline_outputs_darwin_arm64)" ] && echo "output aggregate matches the recorded M1 value" || { echo "::error title=output hash::differs from the recorded M1 value"; rc=1; } ;;
  linux) case "$goto" in "$(want pipeline_outputs_linux_prefix)"*) echo "output aggregate starts with the recorded Linux prefix" ;; *) echo "::error title=output hash::does not start with the recorded Linux prefix $(want pipeline_outputs_linux_prefix)"; rc=1 ;; esac ;;
  *) echo "unknown platform $P"; rc=1 ;;
esac
exit $rc
