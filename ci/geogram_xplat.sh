#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
#
# Pinned, hash-checked build of the patched Geogram and the pipeline CLI geogram_fidelity2, plus the corpus generation and the verdict/hash comparison (the
# "MP24b" stages of the research history; the stage names keep their old numbers so that recorded reports stay readable).
# usage: ci/geogram_xplat.sh <stage>
#   fetch              download Geogram v1.10.1 and the four submodule archives, verify the pinned sha256 values
#   build-chain        one source tree: unpatched, then geogram_mp20.patch, then geogram_mp22.patch (incremental); runners copied to $WORK/geo_{unpatched,mp20,mp22}
#   build-fidelity2    build geogram_fidelity2 (sources checked against lib/geogram/mp24b/FROZEN_SHA256.txt)
#   tools24b           build mesh_compare and every corpus generator needed
#   corpus24b          generate G1, set 3, set 5, the offset/thin families and the fresh f2 families; verify every file against its EXPECTED_SHA256 file
#   score24b           score every family through geogram_fidelity2 (GEOGRAM_RUNNER_THREADS=1)
#   report24b          tables, bars, refusals, and the aggregate hashes
#   mp24b              all of the above in order
# Runs the same way on macOS arm64 (WORK defaults to <repo>/build/work on the external volume) and on ubuntu-24.04 x86_64/aarch64 (CI sets WORK).
# Needs: curl, tar, patch, cmake, ninja or make, g++/c++, python3 and the fetched third-party inputs (third_party/fetch.sh boost shewchuk).
# Hard failures (exit 1): tarball sha256 differs from the pin, patch/patched-file hash differs from its FROZEN_SHA256 file, a restricted component is compiled,
# a generated corpus file differs from its EXPECTED_SHA256 file. Verdicts themselves never fail the script.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
MESH="$ROOT"
DEPS="${EXACTMESH_DEPS:-$ROOT/build/deps}"
BOOST="$DEPS/boost_1_88_0"
PREDICATES_C="$DEPS/shewchuk/predicates.c"
TOOLS_OUT="$ROOT/build/mesh"                # mesh_compare lives here (oracle/score_library.py and score_fidelity2.py look for it)
WORK="${WORK:-$ROOT/build/work}"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu) )}"          # parallel scoring shards
export TMPDIR="$WORK/tmp"
mkdir -p "$WORK" "$TMPDIR" "$WORK/dl" "$TOOLS_OUT"
G_VER=v1.10.1
G_SHA256=24b661c13627d64120efd05217ddc6b12741e2d22698309bb2128416b439f2d1   # recorded in docs/PROVENANCE.md
G_URL=https://github.com/BrunoLevy/geogram/archive/refs/tags/$G_VER.tar.gz
# Submodules are not in GitHub's source tarballs. Commits are the ones recorded in docs/PROVENANCE.md. The archive hashes below are
# what GitHub served on 2026-11-14 (not pinned upstream by anyone); a difference is a warning, the commit id is what pins the content.
SUBS="OpenNL|BrunoLevy/OpenNL|ae782db2db40cc40fcd7659629fc652e5eab1f9e|22bb3fbe404cf109a4b5ea97b2a40908438c33f313a628856d74ecc81dce4934
amgcl|ddemidov/amgcl|93827a00fc926d951c75f08fdb1d491912ff7065|9969cfbf56acfe7fd5e70e0630255b898cfbdbcd800dc90a4e3665e2c6f9bc75
libMeshb|LoicMarechal/libMeshb|88095d5ed04bfaf6bee27b777e7190bebdc72230|17946a0c61a0bdb9be5d012f26425b3fd89f7d46142498caa74d1dec10089b82
rply|diegonehab/rply|4296cc91b5c8c26d4e7d7aac0cee2b194ffc5800|c8b8350352c4935a5ec3cc582082b75e84e5415615c8adadc3acddbf43877409"
FROZEN="$ROOT/patches/geogram/FROZEN_SHA256_mp20.txt"
PATCH="$ROOT/patches/geogram/geogram_mp20.patch"
FROZEN22="$ROOT/patches/geogram/FROZEN_SHA256_mp22.txt"
PATCH22="$ROOT/patches/geogram/geogram_mp22.patch"
SET5_HASHES="$MESH/lib/geogram/mp22b/SET5_HASHES_BEFORE_RUN.txt"
PRISTINE_CPP_SHA256=facff4cf02990f94432b8ba92725977cce8475bd9013bfa85cf778a3f3c9eb24   # pristine v1.10.1 mesh_surface_intersection.cpp (patches/geogram/FROZEN_SHA256_mp20.txt)

say() { echo; echo "=== $* ($(date -u +%H:%M:%S))"; }
die() { echo "::error title=exactmesh::$*" >&2; echo "FATAL: $*" >&2; exit 1; }
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
shacheck() { if command -v sha256sum >/dev/null; then sha256sum -c "$1"; else shasum -a 256 -c "$1"; fi; }   # run inside the dir
download() {  # url dest
  [ -s "$2" ] || curl -fsSL --retry 3 -o "$2" "$1"
}
platform() {
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) echo Linux64-gcc ;;
    Linux-aarch64|Linux-arm64) echo Linux64-gcc-aarch64 ;;
    Darwin-arm64) echo Darwin-aarch64-clang ;;
    *) die "unsupported platform $(uname -s)-$(uname -m)" ;;
  esac
}
arch_tag() { echo "$(uname -s)-$(uname -m)"; }

stage_fetch() {
  say "fetch Geogram $G_VER and verify the pinned tarball sha256"
  download "$G_URL" "$WORK/dl/geogram-$G_VER.tar.gz"
  got="$(sha "$WORK/dl/geogram-$G_VER.tar.gz")"
  echo "pinned  $G_SHA256"; echo "actual  $got"
  if [ "$got" != "$G_SHA256" ]; then
    die "Geogram $G_VER archive sha256 differs from the pin recorded in this script (GitHub regenerates archives; the pin was NOT updated). Stopping."
  fi
  echo "tarball sha256 matches the pin"
  echo "$SUBS" | while IFS='|' read -r n repo commit want; do
    download "https://github.com/$repo/archive/$commit.tar.gz" "$WORK/dl/$n.tar.gz"
    got="$(sha "$WORK/dl/$n.tar.gz")"
    if [ "$got" = "$want" ]; then echo "submodule $n @ ${commit:0:10}: archive sha256 matches the recorded value"
    else echo "::warning title=Geogram submodule archive::$n @ $commit archive sha256 is $got, recorded $want (the commit id still pins the content)"; fi
  done
}

extract_tree() {  # dir
  local D="$1"; rm -rf "${D:?}"; mkdir -p "$D/src"
  tar xzf "$WORK/dl/geogram-$G_VER.tar.gz" -C "$D/src" --strip-components=1
  echo "$SUBS" | while IFS='|' read -r n repo commit want; do
    mkdir -p "$D/src/src/lib/geogram/third_party/$n"
    tar xzf "$WORK/dl/$n.tar.gz" -C "$D/src/src/lib/geogram/third_party/$n" --strip-components=1
  done
}

cmake_build() {  # dir name   configure + build libgeogram.a, check the restricted components were not compiled
  local D="$1" N="$2"
  local GEN=(); if command -v ninja >/dev/null; then GEN=(-G Ninja); else GEN=(-G "Unix Makefiles"); fi
  # Same options as docs/REPRODUCE.md; static library; no GUI; flags repeated explicitly (the platform file also sets them).
  cmake -S "$D/src" -B "$D/build" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DVORPALINE_PLATFORM="$(platform)" \
    -DGEOGRAM_WITH_TETGEN=OFF -DGEOGRAM_WITH_TRIANGLE=OFF -DGEOGRAM_WITH_HLBFGS=OFF \
    -DGEOGRAM_WITH_GRAPHICS=OFF -DGEOGRAM_WITH_LEGACY_NUMERICS=OFF -DGEOGRAM_WITH_LUA=OFF \
    -DGEOGRAM_WITH_TBB=OFF -DGEOGRAM_WITH_FPG=OFF -DGEOGRAM_WITH_GARGANTUA=OFF -DGEOGRAM_LIB_ONLY=ON \
    -DCMAKE_CXX_FLAGS="-frounding-math -ffp-contract=off" -DCMAKE_C_FLAGS="-frounding-math -ffp-contract=off" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 > "$D/cmake.log" 2>&1 || { tail -30 "$D/cmake.log"; die "cmake configure failed ($N)"; }
  grep -E "VORPALINE_BUILD_DYNAMIC|GEOGRAM_WITH_(TETGEN|TRIANGLE|HLBFGS|GRAPHICS):" "$D/build/CMakeCache.txt" || true
  cmake --build "$D/build" -j "$JOBS" > "$D/build.log" 2>&1 || { tail -40 "$D/build.log"; die "geogram build failed ($N)"; }
  [ -s "$D/build/lib/libgeogram.a" ] || die "no static libgeogram.a produced ($N)"
  # restricted components must not be compiled
  python3 -I - "$D/build/compile_commands.json" <<'PY' || die "restricted component compiled ($N)"
import json, re, sys
c = json.load(open(sys.argv[1])); bad = [e["file"] for e in c if re.search(r"third_party/(tetgen|triangle|hlbfgs)/", e["file"], re.I)]
print(f"compiled translation units: {len(c)} (MP20 recorded 147 on macOS); restricted: {len(bad)}")
sys.exit(1 if bad else 0)
PY
}

build_predicates() {
  [ -f "$PREDICATES_C" ] || die "predicates.c missing: run third_party/fetch.sh shewchuk first"
  if [ ! -f "$WORK/libshewchuk_predicates.a" ]; then
    ${CC:-cc} -O3 -DNDEBUG -ffp-contract=off -w -c "$PREDICATES_C" -o "$WORK/predicates.o"; ar rcs "$WORK/libshewchuk_predicates.a" "$WORK/predicates.o"
  fi
}

# Check a FROZEN_SHA256 file of the research history against this tree: old paths (poc/gpu/mesh/...) are mapped to the new layout; the two files that had to be adapted to the new layout
# (lib/geogram/mp24b/build.sh, score_fidelity2.py) are checked against lib/geogram/mp24b/ADAPTED_SHA256.txt instead (see docs/PROVENANCE.md).
check_frozen() {  # frozen-file
  local t="$WORK/frozen.sums"
  grep -E '^[0-9a-f]{64}  ' "$1" | sed -e 's#  poc/gpu/mesh/lib/geogram/mp2[02]/geogram_#  patches/geogram/geogram_#' -e 's#  poc/gpu/mesh/#  #' \
    | grep -v -E '  lib/geogram/mp24b/(build\.sh|score_fidelity2\.py)$' > "$t"
  ( cd "$ROOT" && shacheck "$t" ) || return 1
  ( cd "$ROOT" && shacheck lib/geogram/mp24b/ADAPTED_SHA256.txt ) || return 1
}

stage_tools() {
  say "build mesh_compare (the scoring oracle) into $TOOLS_OUT"
  local INC=(-I"$BOOST")
  mkdir -p "$TOOLS_OUT" "$WORK/bin"
  build_predicates
  if [ ! -x "$TOOLS_OUT/mesh_compare" ]; then
    ${CXX:-c++} -std=c++17 -O3 -DNDEBUG -ffp-contract=off "${INC[@]}" "$MESH/oracle/compare.cpp" "$WORK/libshewchuk_predicates.a" -o "$TOOLS_OUT/mesh_compare"
  else echo "using existing $TOOLS_OUT/mesh_compare"; fi
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off "${INC[@]}" "$MESH/corpus/touch/gen_touch_set5.cpp" "$WORK/libshewchuk_predicates.a" -o "$WORK/bin/gen_touch_set5"
}

shard_score() {  # name runner corpus [ids-file]   -> $WORK/score/<name>.tsv (merged), shards in $WORK/score/<name>_<k>
  local NAME="$1" RUN="$2" C="$3" IDS="${4:-}" S="${SCORE_DIR:-$WORK/score}"; mkdir -p "$S"
  export GEOGRAM_RUNNER_THREADS=1   # as MP18/MP20 run_shards.sh
  if [ -n "$IDS" ]; then cp "$IDS" "$S/ids_$NAME.all"; else tail -n +2 "$C/manifest.tsv" | cut -f1 > "$S/ids_$NAME.all"; fi
  python3 -I - "$S" "$NAME" "$JOBS" <<'PY'
import sys
s, n, k = sys.argv[1], sys.argv[2], int(sys.argv[3]); ids = open(f"{s}/ids_{n}.all").read().split()
for i in range(k): open(f"{s}/ids_{n}_{i}.txt", "w").write(",".join(ids[i::k]))
PY
  local pids=() k p
  for ((k=0; k<JOBS; k++)); do
    [ -s "$S/ids_${NAME}_$k.txt" ] || continue
    ( python3 "${SCORER:-$MESH/oracle/score_library.py}" "${NAME}_$k" "$RUN" "$C" --points 5000 --cases "$(cat "$S/ids_${NAME}_$k.txt")" --outdir "$S/${NAME}_$k" > "$S/log_${NAME}_$k.log" 2>&1 ) &
    pids+=($!)
  done
  local bad=0; for p in "${pids[@]}"; do wait "$p" || bad=1; done
  [ $bad = 0 ] || { tail -20 "$S"/log_${NAME}_*.log; die "scoring shard failed for $NAME"; }
  head -1 "$S/${NAME}_0/results.tsv" > "$S/$NAME.tsv"
  for ((k=0; k<JOBS; k++)); do if [ -f "$S/${NAME}_$k/results.tsv" ]; then tail -n +2 "$S/${NAME}_$k/results.tsv"; fi; done | LC_ALL=C sort -t$'\t' -k1,1 -k2,2 >> "$S/$NAME.tsv"
  grep -h "^summary" "$S"/log_${NAME}_*.log | sort | uniq -c | head -3 || true
}

# ---------------------------------------------------------------- build chain ----------------------------------------------------------------
# One source tree, one full build, then two incremental rebuilds (each patch touches only mesh_surface_intersection.cpp): the runner is copied to
# $WORK/geo_unpatched, geo_mp20 (MP20 patch) and geo_mp22 (MP20 then MP22 patch, in this order) after each step.
stage_build_chain() {
  local D="$WORK/geo_chain" CPP=src/lib/geogram/mesh/mesh_surface_intersection.cpp
  say "build Geogram $G_VER: unpatched, then MP20 patch, then MP22 patch (one tree, incremental)"
  [ -s "$WORK/dl/geogram-$G_VER.tar.gz" ] || stage_fetch
  local want20 want20cpp want22; want20="$(awk '/geogram_mp20.patch/{print $1}' "$FROZEN")"; want20cpp="$(awk '/patched mesh_surface_intersection.cpp/{print $1}' "$FROZEN")"
  want22="$(awk '/geogram_mp22.patch/{print $1}' "$FROZEN22")"
  [ -n "$want20" ] && [ -n "$want20cpp" ] && [ -n "$want22" ] || die "FROZEN_SHA256 files lack a patch hash"
  [ "$(sha "$PATCH")" = "$want20" ] || die "geogram_mp20.patch sha256 differs from mp20/FROZEN_SHA256.txt"
  [ "$(sha "$PATCH22")" = "$want22" ] || die "geogram_mp22.patch sha256 differs from mp22/FROZEN_SHA256.txt"
  echo "patch hashes match the FROZEN files: mp20 $want20, mp22 $want22"
  extract_tree "$D"
  [ "$(sha "$D/src/$CPP")" = "$PRISTINE_CPP_SHA256" ] || die "pristine mesh_surface_intersection.cpp differs from the recorded v1.10.1 hash"
  cmake_build "$D" unpatched
  save_runner() {  # name
    GEOGRAM_DIR="$D" sh "$MESH/lib/geogram/build.sh"
    mkdir -p "$WORK/geo_$1/bin"; cp "$D/bin/geogram_runner" "$WORK/geo_$1/bin/geogram_runner"
    echo "runner geo_$1 sha256 $(sha "$WORK/geo_$1/bin/geogram_runner")"
  }
  save_runner unpatched
  patch -p1 -d "$D/src" < "$PATCH"
  got="$(sha "$D/src/$CPP")"; echo "MP20-patched file sha256 $got (frozen $want20cpp)"
  [ "$got" = "$want20cpp" ] || die "MP20-patched mesh_surface_intersection.cpp differs from the frozen hash"
  say "incremental rebuild with the MP20 patch"
  cmake --build "$D/build" -j "$JOBS" > "$D/build_mp20.log" 2>&1 || { tail -40 "$D/build_mp20.log"; die "geogram rebuild failed (mp20)"; }
  save_runner mp20
  patch -p1 -d "$D/src" < "$PATCH22"
  echo "MP22-patched file sha256 $(sha "$D/src/$CPP") (informational; FROZEN_SHA256.txt of mp22 records only the patch)"
  say "incremental rebuild with the MP22 patch on top"
  cmake --build "$D/build" -j "$JOBS" > "$D/build_mp22.log" 2>&1 || { tail -40 "$D/build_mp22.log"; die "geogram rebuild failed (mp22)"; }
  save_runner mp22
  [ "$(sha "$WORK/geo_unpatched/bin/geogram_runner")" != "$(sha "$WORK/geo_mp20/bin/geogram_runner")" ] || die "mp20 runner identical to unpatched (patch not compiled in)"
  [ "$(sha "$WORK/geo_mp20/bin/geogram_runner")" != "$(sha "$WORK/geo_mp22/bin/geogram_runner")" ] || die "mp22 runner identical to mp20 (patch not compiled in)"
}

stage_corpus5() {
  say "generate set 5, verify the generator source and every output file against the hashes recorded before any run"
  [ -x "$WORK/bin/gen_touch_set5" ] || stage_tools
  local H="$MESH/corpus/touch" d=set5_corpus
  want="$(awk '/gen_touch_set5.cpp/{print $1}' "$SET5_HASHES")"; [ -n "$want" ] || die "no generator hash in SET5_HASHES_BEFORE_RUN.txt"
  [ "$(sha "$H/gen_touch_set5.cpp")" = "$want" ] || die "gen_touch_set5.cpp differs from the hash recorded before the run"
  rm -rf "${WORK:?}/$d"; "$WORK/bin/gen_touch_set5" "$WORK/$d" > "$WORK/$d.gen.log" 2>&1 || { tail -5 "$WORK/$d.gen.log"; die "gen_touch_set5 failed"; }
  tail -1 "$WORK/$d.gen.log"
  if ( cd "$WORK/$d" && shacheck "$H/EXPECTED_SHA256_set5.txt" > "$WORK/$d.verify.log" 2>&1 ); then echo "$d: all $(wc -l < "$H/EXPECTED_SHA256_set5.txt") files match EXPECTED_SHA256_set5.txt"
  else echo "$d: MISMATCH:"; grep -v ': OK$' "$WORK/$d.verify.log" | head -20; die "generated set 5 differs from EXPECTED_SHA256_set5.txt (different generator output on this platform); results would not be comparable. Stopping."; fi
}

# ---------------------------------------------------------------- families (MP24) ----------------------------------------------------------------
FID_DIR="$MESH/corpus/fidelity"
stage_tools24() {
  say "build mesh_compare plus gen_corpus, gen_touch_set3, gen_touch_set5 and gen_fidelity"
  stage_tools
  local INC=(-I"$BOOST")
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off "${INC[@]}" "$MESH/corpus/gen_corpus.cpp" -o "$WORK/bin/gen_corpus"
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off "${INC[@]}" "$MESH/corpus/touch/gen_touch_set3.cpp" "$WORK/libshewchuk_predicates.a" -o "$WORK/bin/gen_touch_set3"
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off "${INC[@]}" "$FID_DIR/gen_fidelity.cpp" -o "$WORK/bin/gen_fidelity"
}
stage_corpus24() {
  say "generate G1, touch set 3, touch set 5 and the MP24 families; verify generator sources and every file against the hashes recorded before the runs"
  [ -x "$WORK/bin/gen_fidelity" ] || stage_tools24
  local want; want="$(awk '/gen_fidelity.cpp/{print $1}' "$FID_DIR/GENERATOR_SHA256.txt")"; [ -n "$want" ] || die "no generator hash in corpus/fidelity/GENERATOR_SHA256.txt"
  [ "$(sha "$FID_DIR/gen_fidelity.cpp")" = "$want" ] || die "gen_fidelity.cpp differs from the hash recorded before the run"
  rm -rf "${WORK:?}/g1_corpus"; "$WORK/bin/gen_corpus" "$WORK/g1_corpus" --many 200 > "$WORK/g1_corpus.gen.log" 2>&1 || die "gen_corpus failed"
  if ( cd "$WORK/g1_corpus" && shacheck "$MESH/corpus/EXPECTED_SHA256.txt" > "$WORK/g1_corpus.verify.log" 2>&1 ); then echo "g1_corpus: all files match corpus/EXPECTED_SHA256.txt"
  else grep -v ': OK$' "$WORK/g1_corpus.verify.log" | head; die "G1 corpus differs from corpus/EXPECTED_SHA256.txt on this platform"; fi
  rm -rf "${WORK:?}/set3_corpus"; "$WORK/bin/gen_touch_set3" "$WORK/set3_corpus" > "$WORK/set3_corpus.gen.log" 2>&1 || { tail -5 "$WORK/set3_corpus.gen.log"; die "gen_touch_set3 failed"; }
  if ( cd "$WORK/set3_corpus" && shacheck "$MESH/corpus/touch/EXPECTED_SHA256_set3.txt" > "$WORK/set3_corpus.verify.log" 2>&1 ); then echo "set3_corpus: all files match EXPECTED_SHA256_set3.txt"
  else grep -v ': OK$' "$WORK/set3_corpus.verify.log" | head; die "set 3 differs from EXPECTED_SHA256_set3.txt on this platform"; fi
  stage_corpus5
  rm -rf "${WORK:?}/fid_corpus"; "$WORK/bin/gen_fidelity" "$WORK/fid_corpus" > "$WORK/fid_corpus.gen.log" 2>&1 || die "gen_fidelity failed"; cat "$WORK/fid_corpus.gen.log"
  if ( cd "$WORK/fid_corpus" && shacheck "$FID_DIR/EXPECTED_SHA256.txt" > "$WORK/fid_corpus.verify.log" 2>&1 ) && [ "$(find "$WORK/fid_corpus" -type f | wc -l | tr -d ' ')" = "$(wc -l < "$FID_DIR/EXPECTED_SHA256.txt" | tr -d ' ')" ]; then
    echo "fid_corpus: all $(wc -l < "$FID_DIR/EXPECTED_SHA256.txt") files match corpus/fidelity/EXPECTED_SHA256.txt"
  else grep -v ': OK$' "$WORK/fid_corpus.verify.log" | head; die "MP24 families differ from corpus/fidelity/EXPECTED_SHA256.txt on this platform (different generator output); results would not be comparable. Stopping."; fi
}

# ---------------------------------------------------------------- MP24b ----------------------------------------------------------------
# geogram_fidelity2 = pipeline v2 (lib/geogram/mp24b): exact scale to max|x| < 2^18, exact re-centring or c = 0 (|centre| <= T x side, T = 2^12 from the development sweep) or a hard-limit refusal.
# Frozen sources: mp24b/FROZEN_SHA256.txt (checked before the build). Fresh acceptance families: corpus/fidelity2 (gen_fidelity2.cpp, EXPECTED_SHA256.txt, GENERATOR_SHA256.txt).
# The MP24 families (G1, offset, thin, set 3, set 5) are re-run through v2. Scored with lib/geogram/mp24b/score_fidelity2.py, GEOGRAM_RUNNER_THREADS=1. Each family is scored once.
FROZEN24B="$MESH/lib/geogram/mp24b/FROZEN_SHA256.txt"
FID2_DIR="$MESH/corpus/fidelity2"
FID2="${FID2_BIN:-$WORK/geo_chain/bin/geogram_fidelity2}"      # FID2_BIN: a planted mutant (lib/geogram/mp24b/run_mutants.sh)
SCORE24B_DIR="${SCORE24B_DIR:-$WORK/score24b}"
FAMS24B="${FAMS24B:-g1:g1_corpus off:fid_corpus/offset thin:fid_corpus/thin set3:set3_corpus set5:set5_corpus f2:fid2_corpus}"
stage_build_fidelity2() {
  say "build geogram_fidelity2 (sources checked against lib/geogram/mp24b/FROZEN_SHA256.txt)"
  [ -f "$WORK/geo_chain/build/lib/libgeogram.a" ] || die "no $WORK/geo_chain (build-chain stage not run)"
  check_frozen "$FROZEN24B" || die "a geogram_fidelity2 source or a frozen input differs from lib/geogram/mp24b/FROZEN_SHA256.txt"
  GEOGRAM_DIR="$WORK/geo_chain" OUTDIR="$WORK/geo_chain/bin" MUTANTS="${MUTANTS:-0}" BOOST="$BOOST" sh "$MESH/lib/geogram/mp24b/build.sh"
  echo "geogram_fidelity2 sha256 $(sha "$FID2")"
}
stage_tools24b() {
  say "build mesh_compare, the MP24 generators and gen_fidelity2"
  stage_tools24
  local INC=(-I"$BOOST")
  ${CXX:-c++} -std=c++17 -O2 -ffp-contract=off "${INC[@]}" "$FID2_DIR/gen_fidelity2.cpp" -o "$WORK/bin/gen_fidelity2"
}
stage_corpus24b() {
  say "generate G1, set 3, set 5, the MP24 families and the MP24b families; verify generator sources and every file against the hashes recorded before the runs"
  [ -x "$WORK/bin/gen_fidelity2" ] || stage_tools24b
  stage_corpus24
  local f want
  for f in gen_fidelity2.cpp EXPECTED_SHA256.txt; do
    want="$(awk -v f="/$f" 'index($2, f) {print $1}' "$FID2_DIR/GENERATOR_SHA256.txt" | head -1)"; [ -n "$want" ] || die "no hash for $f in corpus/fidelity2/GENERATOR_SHA256.txt"
    [ "$(sha "$FID2_DIR/$f")" = "$want" ] || die "$f differs from the hash recorded before the run"
  done
  want="$(awk '/corpus\/fidelity\/gen_fidelity.cpp/{print $1}' "$FID2_DIR/GENERATOR_SHA256.txt")"; [ "$(sha "$MESH/corpus/fidelity/gen_fidelity.cpp")" = "$want" ] || die "gen_fidelity.cpp (included by gen_fidelity2) differs from the recorded hash"
  rm -rf "${WORK:?}/fid2_corpus"; "$WORK/bin/gen_fidelity2" "$WORK/fid2_corpus" > "$WORK/fid2_corpus.gen.log" 2>&1 || die "gen_fidelity2 failed"; cat "$WORK/fid2_corpus.gen.log"
  if ( cd "$WORK/fid2_corpus" && shacheck "$FID2_DIR/EXPECTED_SHA256.txt" > "$WORK/fid2_corpus.verify.log" 2>&1 ) && [ "$(find "$WORK/fid2_corpus" -type f | wc -l | tr -d ' ')" = "$(wc -l < "$FID2_DIR/EXPECTED_SHA256.txt" | tr -d ' ')" ]; then
    echo "fid2_corpus: all $(wc -l < "$FID2_DIR/EXPECTED_SHA256.txt") files match corpus/fidelity2/EXPECTED_SHA256.txt"
  else grep -v ': OK$' "$WORK/fid2_corpus.verify.log" | head; die "MP24b families differ from corpus/fidelity2/EXPECTED_SHA256.txt on this platform (different generator output); results would not be comparable. Stopping."; fi
}
stage_score24b() {
  [ -x "$FID2" ] || die "missing $FID2 (build-fidelity2 stage not run)"
  [ -d "$WORK/fid2_corpus" ] || stage_corpus24b
  export SCORER="$MESH/lib/geogram/mp24b/score_fidelity2.py"
  rm -rf "${SCORE24B_DIR:?}"; mkdir -p "$SCORE24B_DIR"
  export SCORE_DIR="$SCORE24B_DIR"
  local pair n c
  for pair in $FAMS24B; do
    n="${pair%%:*}"; c="$WORK/${pair#*:}"; say "score geogram_fidelity2: $n"
    shard_score "$n" "$FID2" "$c"
  done
}
stage_report24b() {
  say "report (MP24b)"
  python3 -I "$HERE/pipeline_report.py" "$WORK" "$(arch_tag)"
}

case "${1:-mp24b}" in
  fetch) stage_fetch ;;
  build-chain) stage_build_chain ;;
  build-fidelity2) stage_build_fidelity2 ;;
  tools24b) stage_tools24b ;;
  corpus24b) stage_corpus24b ;;
  score24b) stage_score24b ;;
  report24b) stage_report24b ;;
  mp24b) stage_fetch; stage_build_chain; stage_build_fidelity2; stage_tools24b; stage_corpus24b; stage_score24b; stage_report24b ;;
  *) die "unknown stage $1" ;;
esac
