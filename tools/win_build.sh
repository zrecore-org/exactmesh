#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#
# Cross-builds exactmesh for Windows (x86_64, static .exe) with mingw-w64 on Linux (WSL2 or a container). Nothing is run.
#   tools/win_build.sh <outdir> [target ...]       default targets: mp12c_bvh_pip and every unit_* test
# Needs: cmake, ninja, g++-mingw-w64-x86-64-posix, glslc, curl, python3. Downloads the pinned inputs with third_party/fetch.sh (network).
# Two things are different from a native build, both for LLP64 (Windows) targets:
#   1. gpu/shaders/f64soft.inc uses the `ul` suffix, which is 32 bits on Windows; the host copy used for the exe has `ul` rewritten to `ull` (the SPIR-V is compiled
#      from the unmodified file: GLSL has no `ull` suffix). The static_assert in f64soft.inc makes any build without the rewrite fail loudly.
#   2. Shewchuk's predicates.c refers to random(), which mingw lacks (only its unused test-data generators call it): -Drandom=rand.
# The source tree is copied first; the repository is not modified.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; SRC="$(cd "$HERE/.." && pwd)"
OUT="$(mkdir -p "${1:?usage: tools/win_build.sh <outdir> [target ...]}" && cd "$1" && pwd)"; shift
W="$OUT/work"; rm -rf "$W"; mkdir -p "$W"
( cd "$SRC" && tar --exclude=./build --exclude=./storage --exclude=./.git -cf - . ) | tar -C "$W" -xf -
cd "$W"
bash third_party/fetch.sh boost shewchuk catch2 vulkan > "$OUT/fetch.log" 2>&1
cat > "$OUT/toolchain.cmake" <<'T'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(CMAKE_C_FLAGS_INIT "-Drandom=rand")
T
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$OUT/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DEXACTMESH_GPU=ON > "$OUT/cmake.log" 2>&1
if [ $# -eq 0 ]; then
  set -- mp12c_bvh_pip $(ninja -C build -t targets all | sed -n 's/^\(unit_[A-Za-z0-9_]*\):.*/\1/p' | sort -u)
fi
# 1. SPIR-V from the unmodified f64soft.inc (the exe targets fail their static_assert in this pass: expected)
ninja -C build -k 0 "$@" > "$OUT/build_pass1.log" 2>&1 || true
# 2. host copy with ull, then keep the SPIR-V that pass 1 produced
# the tests write scratch files to EXACTMESH_TEST_TMP, a build-directory path baked in at compile time: use the current directory on Windows
sed -i 's#EXACTMESH_TEST_TMP="${CMAKE_BINARY_DIR}/test_work/"#EXACTMESH_TEST_TMP="./"#' tests/CMakeLists.txt
grep -q 'EXACTMESH_TEST_TMP="./"' tests/CMakeLists.txt
# the SPIR-V rules list f64soft.inc as an input; drop that dependency in this copy so ninja does not recompile the shaders from the rewritten file
sed -i 's# "${GPU_SRC}/shaders/f64soft.inc"##' gpu/CMakeLists.txt
grep -q 'shaders/f64soft.inc"' gpu/CMakeLists.txt && { echo "could not drop the f64soft.inc dependency"; exit 1; }
sed -i -E 's/\b((0x[0-9a-fA-F]+)|([0-9]+))ul\b/\1ull/g' gpu/shaders/f64soft.inc
! grep -qE '[0-9a-fA-F]ul\b' gpu/shaders/f64soft.inc
ninja -C build -k 0 "$@" > "$OUT/build.log" 2>&1 && rc=0 || rc=$?
mkdir -p "$OUT/exe"; find build -name '*.exe' -type f -exec cp {} "$OUT/exe/" \;
cp -r build/shaders "$OUT/exe/shaders"   # run with EXACTMESH_SHADER_DIR pointing at this directory
echo "ninja rc=$rc; exe files: $(ls "$OUT/exe" | wc -l); FAILED steps: $(grep -c '^FAILED:' "$OUT/build.log" || true)"
exit $rc
