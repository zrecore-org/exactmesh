# exactmesh

Exact-arithmetic tooling for triangle-mesh Boolean operations (union, intersection, difference) on binary64 meshes:
an exact **input validator**, an exact **scale / re-centre / refuse** rule, a **pinned, patched build of Geogram** that does the actual Boolean, and an independent
**exact oracle and test corpus** that judge the results with integer arithmetic. It also contains GPU kernels (an exact-sign `orient3d` filter in integer-emulated binary64 and a deterministic
LBVH with box-pair emission) and research spikes on 2D and 3D gap-filling.

## Status (read this first)

**Research code, version 0.1.0, not production software.** Everything below was measured on synthetic corpora with exactly known answers; **real meshes were validated on one Thingi10K subset only** (420 pairs on one machine: 0 wrong on the 407 clean pairs, refusals all input defects; `docs/LIMITS.md`). A validator gap found by that run (per-component volume sign and nesting were not checked) is closed in the validator library (`lib/geogram/v1`, tested exactly on 271 generated cases) but **not yet wired into the pipeline CLI, which is built from frozen sources**; crossing component surfaces are still not detected (`docs/LIMITS.md`). On those corpora (9,453 scored rows, Apple M1) the pipeline gives 9,204 PASS, 3 FAIL and 246 REFUSED: 18 refusals are malformed inputs correctly refused and 228 are valid inputs refused
at a documented hard limit (far from the origin, or underflow). The 3 FAIL rows are one pair that a diagnosis shows to be a scoring-rule artifact (output correct up to vertex rounding), but the registered
bar "0 FAIL on the fresh families" is **not met** by it, and this repository says so everywhere rather than rewriting the bar. Read [`docs/LIMITS.md`](docs/LIMITS.md) before relying on any result.

* Exact: the validation decisions, the power-of-two scaling, the re-centring (only when exactly representable), and every check in the test suites.
* Rounded: the result coordinates are rounded to binary64 once when translated back (at most 0.5 ulp, measured); Geogram's own rounding in the working frame is not included.
* Refused, never guessed: invalid input (open, non-manifold, duplicate or zero-area triangles, inverted orientation) and two hard limits (far from the origin without an exact re-centring; scaling that would underflow).
* Not tested: real meshes, Windows, other C++ standard libraries and compilers than the ones listed in `docs/LIMITS.md`, GPUs other than Apple M1, NVIDIA T4 and lavapipe.

## Quick start

```bash
third_party/fetch.sh all                         # Boost headers, Shewchuk predicates, Catch2, Vulkan headers + volk; every archive is sha256-checked first (into build/deps)
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build -L fast                   # the fast suite (unit tests, seconds)
ctest --test-dir build -L mutant                 # planted mutants: each mutant build must FAIL its suite

# acceptance (BDD) tests and the recorded evaluation need the pinned, patched Geogram (about a minute on an M1):
ci/geogram_xplat.sh fetch && ci/geogram_xplat.sh build-chain && ci/geogram_xplat.sh build-fidelity2
cmake -S . -B build -DEXACTMESH_FIDELITY2=$PWD/build/work/geo_chain/bin/geogram_fidelity2 && ctest --test-dir build -L acceptance
ci/geogram_xplat.sh mp24b                        # the whole recorded evaluation (9,453 rows); prints the aggregate hashes
```

Using the pipeline: `geogram_fidelity2 boolean <union|intersection|difference> A.msh B.msh out.msh` (binary mesh format in `harness/mesh_io.h`; exit 0 = result, 3 = refused with the reason on
stderr, 4 = error). More in [`docs/REPRODUCE.md`](docs/REPRODUCE.md).

## Layout

| Path | Contents |
|---|---|
| `harness/` | exact arithmetic (`exact.h`), topology checks, exact point-in-solid, mesh file format (headers) |
| `lib/geogram/` | the pipeline CLI (`mp24b/geogram_fidelity2.cpp`), the exact validator (`mp21b/validator.h`), the Geogram runner |
| `patches/geogram/` | the two frozen Geogram patches (BSD-3-Clause, derivative of Geogram) |
| `oracle/` | Shewchuk-predicate and exact checks, the scoring tool `mesh_compare` |
| `corpus/` | corpus generators and the recorded sha256 of every generated file (generated data is never committed) |
| `gpu/` | soft binary64 (`shaders/f64soft.inc`), orient3d filter, radix sort and LBVH kernels and their host programs |
| `cpu/` | the deterministic CPU BVH reference |
| `ci/` | pinned build and evaluation scripts, recorded aggregate hashes |
| `tests/` | Catch2 tests: `unit/` (fast), `acceptance/` (BDD, need patched Geogram), `gpu/` |
| `research/` | 2D/3D gap-fill spikes and their generators; `research/realmesh/` the real-mesh validation (scripts and results by model id; fetch script with pinned size and sha256; not in CI) |
| `third_party/` | fetch script with pinned hashes; nothing third-party is vendored |
| `docs/` | `PLAN.md` (pre-registered bars), `RESULTS.md`, `LIMITS.md`, `REPRODUCE.md`, `PROVENANCE.md`, parity matrix, license audit |

## Relation to other projects

* [Geogram](https://github.com/BrunoLevy/geogram) (Bruno Levy, Inria; BSD-3-Clause) does the Boolean: exactmesh fetches v1.10.1, builds it **without** the bundled TetGen, Triangle and HLBFGS (restricted licences),
  and applies two small frozen patches that fix defects found by this project's corpora (`docs/LIMITS.md`). The defects are Geogram's, not exactmesh's; the patches have not been sent upstream.
* [Manifold](https://github.com/elalish/manifold) (Emmett Lalish et al.; Apache-2.0) is the comparison engine: it is fast and robust to its tolerance but it snaps below about 1e-12 of the mesh scale, so it loses thin features
  and far-offset geometry that the exact pipeline keeps. It is used only by the optional `ci/manifold_xplat.sh`.
* [Shewchuk's robust predicates](https://www.cs.cmu.edu/~quake/robust.html) (public domain) are the oracle for orientation tests; [Boost.Multiprecision](https://www.boost.org/doc/libs/release/libs/multiprecision/) gives the exact integers.
* Prior work this project re-derived or measured against: Cherchi et al. 2022 (indirect predicates; licence check only), plane-based Booleans (Campen and Kobbelt 2010; Nehring-Wirxel et al. 2021; Trettner et al. 2022; the width bound was re-derived and a kill criterion triggered, `docs/RESULTS.md`).
* Full credits: [`NOTICE`](NOTICE), [`docs/THIRD_PARTY_LICENSES.md`](docs/THIRD_PARTY_LICENSES.md), [`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## Citing

See [`CITATION.cff`](CITATION.cff) (GitHub shows a "Cite this repository" button). Please also cite Geogram and Shewchuk's predicates when you use the pipeline.

## License

Apache-2.0 for all original code (`LICENSE`, `NOTICE`). The two Geogram patch files in `patches/geogram/` are derivative works of Geogram and stay under its BSD-3-Clause licence
(`patches/geogram/LICENSE`). Files whose exact bytes are pinned by recorded hashes carry no in-file header; their licence is declared in `REUSE.toml`. Third-party software is fetched, not vendored.
Not legal advice; the copyright holder named in `NOTICE` is a placeholder pending confirmation.

## Authorship and AI assistance

The research, code, tests and documentation in this repository were produced with extensive help from an AI assistant (Claude, by Anthropic), directed and reviewed by the maintainers at zrecore-org. Every claim is meant to be checkable: recorded results are in `docs/RESULTS.md`, and the corpora, hashes and exact oracles let you reproduce them (`docs/REPRODUCE.md`). Please report anything that does not reproduce.
