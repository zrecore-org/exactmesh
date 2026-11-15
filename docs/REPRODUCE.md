# Reproducing the recorded results

Every command below was run on an Apple M1 Pro (macOS, Apple clang 21, CMake 4.4, Ninja) when this repository was extracted from the research branch (2026-11-14). A different value from the ones listed is a finding to investigate, never a reason to edit `ci/EXPECTED_AGGREGATES.txt`.

## One command per result

| Result | Command | Expected |
|---|---|---|
| Third-party inputs, pinned | `third_party/fetch.sh all` | every archive "ok" (sha256 checked before unpacking) |
| Fast suite | `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build -L fast` | all pass (about a second) |
| Planted mutants are caught | `ctest --test-dir build -L mutant` | 12 of 12 (each mutant build fails its suite: 6 before V1, plus 3 V1 unit mutants and 3 V1 corpus-checker mutants); the 6 pipeline mutants (`MUTANTS=1 ci/geogram_xplat.sh build-fidelity2`, binaries `geogram_fidelity2_mut_*`, incl. `NOV1`) each fail `EXACTMESH_FIDELITY2=<mutant> build/mesh/acceptance_tests "[acceptance]"` |
| Acceptance (BDD) | `ci/geogram_xplat.sh fetch build-chain` ... see below; then `ctest --test-dir build -L acceptance` | 10 scenarios, 257 assertions pass (V2; 182 before V2, 8 scenarios and 157 assertions before V1) |
| Pipeline v2 evaluation, 9,453 rows | `ci/geogram_xplat.sh mp24b` then `ci/check_aggregates.sh build/work/report24b darwin-arm64` | verdict-list hash `e2edd6f7d6ad9862e6cdcc3ff77ea962ccff689893a7baca0d291d673e20a184`; output aggregate `a46b1a3b8b6f3049d252aba52dc2be1a865c71d67293c01938dc315a0ce9a990` (M1; Linux outputs start `5f17783a`) |
| Manifold comparison (G1, 93 rows) | `ci/manifold_xplat.sh all` | aggregate `0bb7453ba7d7d9dbdbf1cf5331e145b80d6cb00bf4fec703b2a4219041f57642` in `build/work_manifold/report/aggregate_sha256.txt` (M1) |
| LBVH pair lists | `cmake -S . -B build -DEXACTMESH_GPU=ON && cmake --build build && tools/with_gpu_lock.sh tools/run_gpu.sh mp10_lbvh device 0.1` (and `... device 1.0`) | `determinism: 20 repeats byte-identical (hash 9d4de5d07a4f1308)` at scale 0.1, `f474f69f005abdb8` at scale 1.0 |
| GPU tests | `tools/with_gpu_lock.sh ctest --test-dir build -L gpu` (set `VK_ICD_FILENAMES` to your Vulkan driver) | 2 test cases pass (orient filter on the device, LBVH byte-for-byte against the CPU reference) |
| LBVH planted mutants | `tools/with_gpu_lock.sh ctest --test-dir build -L lbvhmut` | all 3 caught; kept out of the lavapipe CI job because a mutant of the tree build can hang a kernel (a fence timeout on the M1) |
| V1 validator corpus (64 legit + 207 invalid) | `ctest --test-dir build -R corpus_v1`; `build/mesh/check_v1 set build/test_work/v1 --list \| shasum -a 256` | generator output matches `corpus/v1/EXPECTED_SHA256.txt`; 0 false refusals, 0 wrong accepts; verdict-list hash `5418cee9a425897bc73de284fd6d96d2e6603fbba1cecef85555699900ff7049` (Release and Debug+ASan/UBSan) |
| V1 against the older corpora | `build/mesh/check_v1 compare build/work/g1_corpus build/work/fid_corpus build/work/fid2_corpus build/work/set3_corpus build/work/set5_corpus` | 0 files whose verdict or reason changed |
| V1 through the pipeline CLI (V2), bar b | `python3 -I lib/geogram/v1/check_v1_cli.py build/work/geo_chain/bin/geogram_fidelity2 build/test_work/v1 <scratch dir>` | 3 known-limit inputs + 271 cases, 2,192 CLI runs, 0 mismatches (needs the corpus from `ctest -R corpus_v1` in `build/test_work/v1`) |
| Cavity operands through the CLI (V2), bar c | `lib/geogram/v1/run_cavity_pairs.sh build/work/geo_chain/bin/geogram_fidelity2 build/test_work/v1 <work dir>` | 768 rows: 592 PASS, 168 REFUSED (162 hard limit, 6 Geogram exceptions), 8 FAIL (all `one_ulp_cavity`); bar not met, see `RESULTS.md` V2 |
| Per-row comparison with a saved baseline (V2), bar a | `python3 -I lib/geogram/v1/compare_rows.py <saved score24b dir> build/work/score24b` | 0 of 9,453 rows differ |
| V3 diagnosis, instrumented CLI (needs the built Geogram tree) | `GEOGRAM_DIR=build/work/geo_chain BOOST=build/deps/boost_1_88_0 OUTDIR=build/work/v3/bin sh lib/geogram/v3/build_v3.sh`; then `lib/geogram/v3/v3_batch.sh <v3 cli> build/work/v2_cavity/pairs build/work/v2_cavity/run <work>` | the output hash of every analysed row equals the recorded one (gate: 8 of 8 cavity rows, 49 of 49 control rows); exact volumes as in `RESULTS.md` V3 |
| V3 non-cavity control | `lib/geogram/v3/run_control_pairs.sh build/work/geo_chain/bin/geogram_fidelity2 <work dir>` | 402 rows: 290 PASS, 49 FAIL, 63 REFUSED (27 of them `!point_outside_boundary`) |
| V3 exception probes | `python3 -I lib/geogram/v3/v3_probes.py <v3 cli> build/work/v2_cavity/pairs <scratch>` | ladder in `RESULTS.md` V3 (c) |
| V3 re-score with the `exactinit()` copy | `c++ -std=c++17 -O3 -DNDEBUG -ffp-contract=off -I build/deps/boost_1_88_0 lib/geogram/v3/compare_exactinit.cpp build/libshewchuk_predicates.a -o <bin>`; `python3 -I lib/geogram/v3/rescore.py <bin> <pairs dir> <results dirs>` | cavity run: 4 rows FAIL -> PASS; the 9,453 recorded rows: 0 verdicts change |
| Corpus generators | `ctest --test-dir build -R corpus` and `research/corpus*/gen*.sh --check` | all files match the recorded `EXPECTED_SHA256*.txt` |

`ci/geogram_xplat.sh` stages: `fetch`, `build-chain`, `build-fidelity2`, `tools24b`, `corpus24b`, `score24b`, `report24b`, `mp24b` (all in order). The Geogram build is reused when its stamp (pins, patch hashes, platform, script) is unchanged; `FORCE_REBUILD=1` rebuilds.
`WORK=<dir>` moves all build and score output (default `build/work`; `build/` is git-ignored and should live on a large volume). `JOBS=<n>` sets the number of scoring shards.

## Pinned versions

| Item | Version | Pin |
|---|---|---|
| Geogram | v1.10.1, commit `bbcacba146b357926b5524111f56cc70c42182a7` | tarball sha256 `24b661c13627d64120efd05217ddc6b12741e2d22698309bb2128416b439f2d1` |
| Geogram submodules | OpenNL `ae782db2...`, amgcl `93827a00...`, libMeshb `88095d5e...`, rply `4296cc91...` | archive sha256 values in `ci/geogram_xplat.sh` (a difference is a warning; the commit id pins the content) |
| Geogram patches | `geogram_mp20.patch` `4074ad1f...`, `geogram_mp22.patch` `d2efc124...` | `patches/geogram/FROZEN_SHA256_mp2*.txt`; the MP20-patched `mesh_surface_intersection.cpp` must hash to `bc6b3d36...` |
| Geogram build options | static, `GEOGRAM_WITH_TETGEN/TRIANGLE/HLBFGS/GRAPHICS/LEGACY_NUMERICS/LUA/TBB/FPG/GARGANTUA=OFF`, `-frounding-math -ffp-contract=off` | 147 translation units compiled, none from the restricted components (checked) |
| Boost | 1.88.0 headers (header-only multiprecision) | sha256 `3621533e820dcab1e8012afd583c0c73cf0f77694952b81352bf38c1488f9cb4`, the value published on boost.org's release page |
| Shewchuk `predicates.c` | public domain | sha256 `f8662c3f407d1c1c5dcd4dd49ea8b8ddd801a71827d65734206ddc3741792029` (trust on first use, `oracle/SHA256SUMS`) |
| Catch2 | v3.16.1 | archive sha256 `d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d` |
| Vulkan-Headers | v1.3.283 | archive sha256 `a76ff77815012c76abc9811215c2167128a73a697bcc23948e858d1f7dd54a85` |
| volk | vulkan-sdk-1.3.283.0 | archive sha256 `872035f1f26c53b218632a3a8dbccbd276710aaabafb9bb1bc1a6c0633ee6aab` |
| Manifold (optional) | v3.5.5, commit `32271a1a999ce721ffaf5ab10e9746ceeb696547` (git clone, commit-pinned); Clipper2 commit `46f639177fe418f9689e8ddb74f08a870c71f5b4` fetched by Manifold's CMake and checked | commits |

GitHub regenerates source archives from time to time; if a GitHub archive hash changes, the fetch stops. Recompute, compare with the upstream tag, and record the change in `PROVENANCE.md` before updating a pin.

## Notes on the hash records

* V2 re-froze `geogram_fidelity2.cpp`: `lib/geogram/mp24b/FROZEN_SHA256.txt` stays verbatim (its `geogram_fidelity2.cpp` line is the source of the recorded evaluation) and `lib/geogram/mp24b/FROZEN_SHA256_V2.txt` holds the before and after hashes; `ci/geogram_xplat.sh` checks both. The recorded aggregates (`e2edd6f7...`, `a46b1a3b...`) did not change.
* The files `lib/geogram/mp24b/FROZEN_SHA256.txt`, `corpus/*/GENERATOR_SHA256.txt` and `lib/geogram/mp2*/SET*_HASHES_BEFORE_RUN.txt` are the records made **before** the corresponding runs. They are kept verbatim (paths in them name the research-history layout); `ci/geogram_xplat.sh` maps the paths when checking.
  Files whose bytes they pin are byte-identical to the research branch. Four build/scoring scripts had to change for the new layout and are checked against `lib/geogram/mp24b/ADAPTED_SHA256.txt` instead (`PROVENANCE.md`).
* The recorded aggregates are platform-specific because Geogram's outputs differ in the last bits between libc++ and libstdc++; the verdicts do not.

## What was run for the extraction (2026-11-14)

Local runs of this repository's own scripts (not GitHub Actions: this repository's CI has not run yet):

* **Apple M1 Pro, macOS, Apple clang 21:** fast suite (6 CTest entries, 7 with the GPU build: `unit_core` 15 Catch2 test cases, `unit_validator` 3, `unit_rules` 7, about 370,000 assertions in all, two corpus checks, `predicates_check`), 6 of 6 planted mutants caught, acceptance 8 scenarios (157 assertions) green, and each of the 5 planted pipeline mutants
  (`NOSCALE`, `SCALE19`, `C0`, `NOBACK`, `TLARGE`) fails the acceptance suite. **Discriminator matrix** (which scenarios fail when the pipeline CLI is replaced; the scenarios share the CLI contract): unpatched Geogram runner fails the vertex-contact, nested, bound-trap, far-offset, hard-limit and malformed scenarios;
  the MP20 and MP22 runners (no scaling, no re-centring, no validation) fail all of those except vertex contact; `NOSCALE` fails nested, bound-trap and underflow; `SCALE19` fails bound-trap; `C0` fails far-offset and far-refusal; `NOBACK` fails nested, bound-trap and far-offset; `TLARGE` fails far-refusal;
  the Manifold runner fails the 2^-40 sliver, far-offset, malformed and hard-limit scenarios. The determinism scenario is a property check that none of these discriminators exercises. `ci/geogram_xplat.sh mp24b` reproduced both recorded pipeline hashes (verdict list `e2edd6f7...`, M1 output aggregate `a46b1a3b...`) twice, from a clean build and from a reused one.
  `ci/manifold_xplat.sh all` reproduced `0bb7453b...`. `mp10_lbvh` reproduced `9d4de5d07a4f1308` (scale 0.1) and `f474f69f005abdb8` (scale 1.0) through the shared GPU lock; `ctest -L gpu` (orient filter on the device, LBVH) and `ctest -L lbvhmut` (3 planted LBVH mutants) pass.
  All four research corpus generators (`research/corpus*/gen*.sh --check`) and the G1 generator match their recorded hashes.
* **Linux aarch64 (native container) and Linux x86_64 (the same image under emulation), Ubuntu 24.04, g++ 13.3, libstdc++:** the steps of the `fast`, `acceptance`, `verdicts` and `gpu-lavapipe` CI jobs, with the exact commands of `ci.yml`,
  on an empty `build/deps` (so the fetch path ran): fast suite 6 of 6 and mutants 6 of 6 green; acceptance green; verdict-list hash `e2edd6f7...` identical to the M1; output aggregate
  `5f17783af5febfe48978fe3a48122eb494dbc525974b55b5f475c2514bce2007` **identical on aarch64 and x86_64** (every row of the output hash list identical), which extends the prefix `5f17783a` recorded by the research branch.
  lavapipe on aarch64: `ctest -L gpu` green and the LBVH pair-list hash at scale 0.1 is `9d4de5d07a4f1308`, as on the M1. Not re-run after the last fix: the lavapipe step on x86_64.
  Findings from these runs: a bare Ubuntu image needs `zlib1g-dev` for Geogram's headers (hosted runner images carry it; the CI package lists include it); the multi-stage command lines of the scripts did not work until the scripts looped over their arguments;
  the GPU host programs looked for their SPIR-V relative to the working directory (fixed).
* Not run: GitHub Actions, macOS-hosted runner, lavapipe, the T4, Windows.

* **V1 (2026-11-14, Apple M1, Apple clang, Release `-O2` and Debug with `-fsanitize=address,undefined`):** fast suite 10 of 10 (Debug build: 9 of 9, no GPU test), mutants 12 of 12, acceptance 10 scenarios / 182 assertions. The CTest counts, test-case counts (`unit_validator` 5, `unit_validator_v1` 4) and the "6 CTest entries" / "6 of 6" numbers in the extraction notes above are historical and describe the state before V1. Not run for V1: Linux x86_64 and aarch64, GitHub Actions, the real-mesh set.

* **V2 (2026-11-14, Apple M1, Release `-O2`; Debug with ASan/UBSan for the fast suite):** `ci/geogram_xplat.sh mp24b` (with `MUTANTS=1`) re-run in full after wiring V1 into `geogram_fidelity2`: verdict-list hash `e2edd6f7...` and output aggregate `a46b1a3b...` unchanged, 0 of 9,453 rows differ from the saved baseline; fast 10 of 10 (Debug 9 of 9), mutants 12 of 12 in both, acceptance 10 scenarios / 257 assertions, 6 pipeline mutants each fail the acceptance suite. Bar (c) (cavity operands) was NOT met: 8 FAIL and 6 Geogram exceptions in the `one_ulp_cavity` family, unchanged from the pre-V2 CLI. Not run for V2: Linux x86_64 and aarch64, GitHub Actions, the real-mesh set, `run_mutants.sh`. The fast suite in the Release build included the GPU orient-filter test (the build has the GPU option on).

