#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#
# Fetches the pinned third-party inputs into $EXACTMESH_DEPS (default: <repo>/build/deps; build/ is git-ignored) and verifies every archive against
# the sha256 pinned below BEFORE it is unpacked. Nothing fetched is ever committed. A hash mismatch is a hard failure; the pin is never updated silently.
#   third_party/fetch.sh [boost|shewchuk|catch2|vulkan|all]...    (any number of items, in order; default: all)
# Result layout:  $DEPS/boost_1_88_0/boost/...   $DEPS/shewchuk/predicates.c   $DEPS/Catch2-3.16.1   $DEPS/Vulkan-Headers-1.3.283   $DEPS/volk-vulkan-sdk-1.3.283.0
# Geogram and Manifold have their own pinned fetch stages in ci/geogram_xplat.sh and ci/manifold_xplat.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"
DEPS="${EXACTMESH_DEPS:-$ROOT/build/deps}"; DL="$DEPS/dl"; mkdir -p "$DL"
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
die() { echo "FATAL: $*" >&2; exit 1; }
fetch() {   # url file sha256   (download once, verify every time)
  [ -s "$DL/$2" ] || curl -fsSL --retry 3 -o "$DL/$2" "$1"
  got="$(sha "$DL/$2")"
  [ "$got" = "$3" ] || { rm -f "$DL/$2"; die "$2: sha256 $got differs from the pin $3 (nothing was unpacked)"; }
  echo "ok  $2  $3"
}

# Boost 1.88.0, header tarball; the sha256 is the one published on https://www.boost.org/releases/1.88.0/ (checked 2026-11-14). Only boost/ (headers) is unpacked.
BOOST_SHA256=3621533e820dcab1e8012afd583c0c73cf0f77694952b81352bf38c1488f9cb4
get_boost() {
  [ -f "$DEPS/boost_1_88_0/boost/multiprecision/cpp_int.hpp" ] && { echo "boost: present"; return; }
  fetch https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz boost_1_88_0.tar.gz "$BOOST_SHA256"
  tar xzf "$DL/boost_1_88_0.tar.gz" -C "$DEPS" boost_1_88_0/boost boost_1_88_0/LICENSE_1_0.txt
}
# Shewchuk's predicates.c (public domain). Same script and SHA256SUMS as the research history had; the hash was recorded on first fetch (trust on first use).
get_shewchuk() { DEPS="$DEPS" "$ROOT/oracle/fetch.sh"; }
# Catch2 v3.16.1 (BSL-1.0). GitHub regenerates source archives; the sha256 below is what GitHub served on 2026-11-14.
CATCH2_SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
get_catch2() {
  [ -f "$DEPS/Catch2-3.16.1/CMakeLists.txt" ] && { echo "catch2: present"; return; }
  fetch https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz catch2-v3.16.1.tar.gz "$CATCH2_SHA256"
  tar xzf "$DL/catch2-v3.16.1.tar.gz" -C "$DEPS"
}
# Vulkan-Headers v1.3.283 (tag object 19935fe710d147d48b2dd5f5071392346347450b) and volk vulkan-sdk-1.3.283.0 (tag object 3a8068a57417940cf2bf9d837a7bb60d015ca2f1), for the GPU host programs.
VKH_SHA256=a76ff77815012c76abc9811215c2167128a73a697bcc23948e858d1f7dd54a85
VOLK_SHA256=872035f1f26c53b218632a3a8dbccbd276710aaabafb9bb1bc1a6c0633ee6aab
get_vulkan() {
  [ -f "$DEPS/Vulkan-Headers-1.3.283/include/vulkan/vulkan.h" ] || { fetch https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.283.tar.gz vulkan-headers-v1.3.283.tar.gz "$VKH_SHA256"; tar xzf "$DL/vulkan-headers-v1.3.283.tar.gz" -C "$DEPS"; }
  [ -f "$DEPS/volk-vulkan-sdk-1.3.283.0/volk.c" ] || { fetch https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.3.283.0.tar.gz volk-vulkan-sdk-1.3.283.0.tar.gz "$VOLK_SHA256"; tar xzf "$DL/volk-vulkan-sdk-1.3.283.0.tar.gz" -C "$DEPS"; }
}
[ $# -gt 0 ] || set -- all
for item in "$@"; do
  case "$item" in
    boost) get_boost ;; shewchuk) get_shewchuk ;; catch2) get_catch2 ;; vulkan) get_vulkan ;;
    all) get_boost; get_shewchuk; get_catch2; get_vulkan ;;
    *) die "unknown item $item" ;;
  esac
done
