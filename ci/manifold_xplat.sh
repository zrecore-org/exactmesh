#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#
# Optional comparison tool: scores Manifold v3.5.5 (SERIAL build, -ffp-contract=off, MANIFOLD_PAR=OFF) on the G1 corpus (31 adversarial pairs x 3 operations = 93 rows) with
# oracle/score_library.py (--points 5000, serial), runs every pair twice, compares with lib/manifold/scoring_results.tsv (the recorded Apple M1 run) and prints the
# aggregate sha256 of the outputs. Manifold is NOT part of the exactmesh pipeline; it is the reference the pipeline is compared against.
# usage: ci/manifold_xplat.sh <stage>...   stages: fetch | build | tools | corpus | score | rerun | report | all
# WORK defaults to <repo>/build/work_manifold. Needs: git, cmake, ninja or make, g++/c++, python3, curl and the fetched inputs (third_party/fetch.sh boost shewchuk).
# Hard failures (exit 1): checked-out Manifold commit or fetched Clipper2 commit differs from the pin, a build fails, a generated corpus file differs from corpus/EXPECTED_SHA256.txt.
# Verdicts and hash differences never fail the script.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MESH="$ROOT"
DEPS="${EXACTMESH_DEPS:-$ROOT/build/deps}"; BOOST="$DEPS/boost_1_88_0"; PREDICATES_C="$DEPS/shewchuk/predicates.c"
TOOLS_OUT="$ROOT/build/mesh"
WORK="${WORK:-$ROOT/build/work_manifold}"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu) )}"
export TMPDIR="$WORK/tmp"
mkdir -p "$WORK" "$TMPDIR"
M_TAG=v3.5.5
M_COMMIT=32271a1a999ce721ffaf5ab10e9746ceeb696547      # docs/PROVENANCE.md
C_COMMIT=46f639177fe418f9689e8ddb74f08a870c71f5b4      # Clipper2, fetched by Manifold's CMake (recorded in docs/PROVENANCE.md); read back below
M_URL=https://github.com/elalish/manifold
SRC="$WORK/src"; BLD="$WORK/buildA"; RUN="$WORK/runA/manifold_runner"; CORPUS="$WORK/corpus"; S="$WORK/score"
REF="$MESH/lib/manifold/scoring_results.tsv"

say() { echo; echo "=== $* ($(date -u +%H:%M:%S))"; }
die() { echo "::error title=manifold::$*" >&2; echo "FATAL: $*" >&2; exit 1; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
shacheck() { if command -v sha256sum >/dev/null; then sha256sum -c "$1"; else shasum -a 256 -c "$1"; fi; }

stage_fetch() {
  say "fetch Manifold $M_TAG and verify the pinned commit"
  # No tarball sha256 is pinned anywhere in the repo (docs/PROVENANCE.md pin the commit id only, and the M1 build used git clone + checkout).
  # So: same method as the M1 (git clone + checkout of the pinned commit); the commit id is a content hash of the whole tree.
  rm -rf "$SRC"
  git clone --quiet "$M_URL" "$SRC"
  git -C "$SRC" checkout --quiet "$M_COMMIT"
  got="$(git -C "$SRC" rev-parse HEAD)"; echo "pinned $M_COMMIT"; echo "actual $got"
  [ "$got" = "$M_COMMIT" ] || die "Manifold HEAD differs from the pinned commit (pin NOT updated)"
  echo "tag $M_TAG resolves to: $(git -C "$SRC" rev-parse "$M_TAG^{commit}" 2>/dev/null || echo 'tag not found')"
  grep -E "^\s*VERSION|MANIFOLD_VERSION" "$SRC/CMakeLists.txt" | head -3 || true
}

stage_build() {
  say "configure + build Manifold, serial, -ffp-contract=off (the recorded M1 build A)"
  [ -d "$SRC/.git" ] || stage_fetch
  local GEN=(-G "Unix Makefiles"); command -v ninja >/dev/null && GEN=(-G Ninja)
  rm -rf "$BLD"
  # Options exactly as the recorded M1 build (MANIFOLD_EXPORT is ignored at this tag, kept for fidelity). Clipper2 is fetched by Manifold's CMake (FetchContent).
  cmake -S "$SRC" -B "$BLD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DMANIFOLD_PAR=OFF -DMANIFOLD_PYBIND=OFF \
    -DMANIFOLD_TEST=OFF -DMANIFOLD_EXPORT=OFF -DMANIFOLD_CBIND=OFF -DMANIFOLD_CROSS_SECTION=ON -DMANIFOLD_USE_BUILTIN_CLIPPER2=ON \
    -DCMAKE_CXX_FLAGS=-ffp-contract=off > "$WORK/cmake.log" 2>&1 || { tail -30 "$WORK/cmake.log"; die "cmake configure failed"; }
  echo "dependencies fetched by Manifold's CMake:"; ls "$BLD/_deps" | sed 's/^/  /'
  for d in "$BLD"/_deps/*-src; do
    [ -d "$d" ] || continue
    echo "  $(basename "$d"): $(git -C "$d" rev-parse HEAD 2>/dev/null || echo 'not a git checkout')"
  done
  local cg; cg="$(git -C "$BLD/_deps/clipper2-src" rev-parse HEAD 2>/dev/null || true)"
  echo "Clipper2 pinned $C_COMMIT"; echo "Clipper2 actual $cg"
  [ "$cg" = "$C_COMMIT" ] || die "fetched Clipper2 commit differs from the pin recorded in docs/PROVENANCE.md (pin NOT updated)"
  ls "$BLD"/_deps | grep -v -E "^clipper2" | sed 's/^/  unexpected dependency: /' || true
  cmake --build "$BLD" -j "$JOBS" > "$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log"; die "manifold build failed"; }
  [ -s "$BLD/src/libmanifold.a" ] && [ -s "$BLD/_deps/clipper2-build/libClipper2.a" ] || die "libmanifold.a or libClipper2.a missing"
  # runner: same command line as the recorded M1 build A
  mkdir -p "$WORK/runA"
  ${CXX:-c++} -std=c++17 -O2 -DNDEBUG -ffp-contract=off -DMANIFOLD_VERSION_STR='"3.5.5"' -I"$SRC/include" "$MESH/lib/manifold/runner.cpp" \
    "$BLD/src/libmanifold.a" "$BLD/_deps/clipper2-build/libClipper2.a" -o "$RUN"
  echo "compiler: $(${CXX:-c++} --version | head -1)"; echo "runner sha256 $(sha "$RUN")"
  "$RUN" measure "$CORPUS/primitives/icosphere_3.msh" 2>&1 | head -3 || true
}

stage_tools() {   # oracle (mesh_compare) and corpus generator, same flags as ci/geogram_xplat.sh (-ffp-contract=off)
  say "build mesh_compare and gen_corpus"
  local B="$BOOST" OUT="$TOOLS_OUT" P="$PREDICATES_C"
  local INC=()
  [ -f "$P" ] || die "predicates.c missing: run third_party/fetch.sh shewchuk first"
  mkdir -p "$OUT" "$WORK/bin"
  if [ ! -f "$WORK/libshewchuk_predicates.a" ]; then
    ${CC:-cc} -O3 -DNDEBUG -ffp-contract=off -w -c "$P" -o "$WORK/predicates.o"; ar rcs "$WORK/libshewchuk_predicates.a" "$WORK/predicates.o"
  fi
  if [ ! -x "$OUT/mesh_compare" ]; then
    ${CXX:-c++} -std=c++17 -O3 -DNDEBUG -ffp-contract=off -I"$B" "$MESH/oracle/compare.cpp" "$WORK/libshewchuk_predicates.a" -o "$OUT/mesh_compare"
  else echo "using existing $OUT/mesh_compare"; fi
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off -I"$B" "$MESH/corpus/gen_corpus.cpp" -o "$WORK/bin/gen_corpus"
}

stage_corpus() {
  say "generate the G1 corpus (gen_corpus --many 200) and verify every file against corpus/EXPECTED_SHA256.txt"
  [ -x "$WORK/bin/gen_corpus" ] || stage_tools
  rm -rf "$CORPUS"; "$WORK/bin/gen_corpus" "$CORPUS" --many 200 > "$WORK/corpus.gen.log" 2>&1 || { tail -5 "$WORK/corpus.gen.log"; die "gen_corpus failed"; }
  local n; n="$(cd "$CORPUS" && find . -type f | wc -l | tr -d ' ')"
  if ( cd "$CORPUS" && shacheck "$MESH/corpus/EXPECTED_SHA256.txt" > "$WORK/corpus.verify.log" 2>&1 ) && [ "$n" = "$(wc -l < "$MESH/corpus/EXPECTED_SHA256.txt" | tr -d ' ')" ]; then
    echo "corpus: all $n files match EXPECTED_SHA256.txt"
  else
    echo "corpus: MISMATCH ($n files generated):"; grep -v ': OK$' "$WORK/corpus.verify.log" | head -20
    die "generated G1 corpus differs from EXPECTED_SHA256.txt (different generator output on this platform, a FINDING); results would not be comparable. Stopping."
  fi
}

stage_score() {
  [ -d "$CORPUS" ] || stage_corpus
  [ -x "$RUN" ] || die "missing $RUN (build stage not run)"
  say "score the G1 corpus with oracle/score_library.py (serial, --points 5000, as the M1 run)"
  rm -rf "$S"; mkdir -p "$S"
  python3 "$MESH/oracle/score_library.py" manifold "$RUN" "$CORPUS" --points 5000 --outdir "$S/run1" > "$S/log_run1.log" 2>&1 || { tail -20 "$S/log_run1.log"; die "scoring failed"; }
  grep "^summary" "$S/log_run1.log"
}

stage_rerun() {   # second independent run of every pair, for run-to-run identity (exit code and output sha256)
  [ -x "$RUN" ] || die "missing $RUN"
  say "second run of every pair (run-to-run identity)"
  mkdir -p "$S/run2"; : > "$S/run2.tsv"
  tail -n +2 "$CORPUS/manifest.tsv" | cut -f1 | while read -r id; do
    for op in union intersection difference; do
      out="$S/run2/$id.$op.msh"; rm -f "$out"; rc=0
      "$RUN" boolean "$op" "$CORPUS/adversarial/${id}_A.msh" "$CORPUS/adversarial/${id}_B.msh" "$out" > /dev/null 2>&1 || rc=$?
      h=-; [ -f "$out" ] && h="$(sha "$out")"
      printf '%s\t%s\t%s\t%s\n' "$id" "$op" "$rc" "$h" >> "$S/run2.tsv"
    done
  done
  wc -l < "$S/run2.tsv"
}

arch_tag() { echo "$(uname -s)-$(uname -m)"; }
stage_report() { say "report"; python3 -I "$HERE/manifold_report.py" "$WORK" "$(arch_tag)" "$REF"; }

[ $# -gt 0 ] || set -- all
for stage in "$@"; do
  case "$stage" in
    fetch) stage_fetch ;;
    build) stage_build ;;
    tools) stage_tools ;;
    corpus) stage_corpus ;;
    score) stage_score ;;
    rerun) stage_rerun ;;
    report) stage_report ;;
    all) stage_fetch; stage_tools; stage_corpus; stage_build; stage_score; stage_rerun; stage_report ;;
    *) die "unknown stage $stage" ;;
  esac
done
