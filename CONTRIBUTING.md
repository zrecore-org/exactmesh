# Contributing

Thank you for helping. This is research code with an unusual rule: **results are pre-registered**, so how you change code and tests matters as much as the code.

## Sign-off (DCO)

Every commit must carry a `Signed-off-by: Your Name <you@example.org>` line (`git commit -s`), certifying the [Developer Certificate of Origin 1.1](https://developercertificate.org/): you wrote the change or have the right to submit it under
the project's licence (Apache-2.0; the two patch files under `patches/geogram/` stay BSD-3-Clause because they derive from Geogram). New source files carry
`SPDX-License-Identifier: Apache-2.0` and a short copyright line. Do not add third-party source to the repository: fetch it through `third_party/fetch.sh` with a pinned sha256.

## Test expectations

* **Fast suite must stay green and fast**: `ctest --test-dir build -L fast` (seconds) and `-L mutant` (every planted mutant build must still FAIL its suite; a mutant that is no longer caught means a test got weaker).
* **Write the test first** for behaviour changes (unit tests in `tests/unit/`, Catch2 v3, `[unit]` tag). A check that cannot fail proves nothing: when you add a check, add or extend a planted mutant that it catches.
* **Acceptance tests** (`tests/acceptance/`, `[acceptance]`, BDD `SCENARIO / GIVEN / WHEN / THEN`) map one to one onto registered bars and need the pinned, patched Geogram (`ci/geogram_xplat.sh build-chain build-fidelity2`). They are never part of the fast suite.
* **GPU tests** (`[gpu]`) need a Vulkan device (lavapipe is enough). On a machine whose GPU is shared, run them through `tools/with_gpu_lock.sh`; the hosted T4 job (`gpu-t4.yml`) is manual only and follows `tools/t4_ci_turn.sh`.
* Floating-point code is compiled with `-ffp-contract=off` and without fast-math; the recorded hashes depend on it.
* Exactness claims need an exact oracle (`harness/exact.h`), not a tolerance. Include the input pair when you report a wrong result.

## How pre-registered bars work

1. A **bar** (what counts as pass, on which corpus, with which kill criterion) is written down in `docs/PLAN.md` and committed **before** the run it governs; corpus generators and their `EXPECTED_SHA256` files are committed (and their hashes recorded) **before** any library output on them is scored.
2. The bar is then evaluated once per platform. Outcomes are reported as measured, including failures: a bar that is not met stays "not met" in `docs/LIMITS.md` and `docs/RESULTS.md` (the current example is MP24b, 3 rows).
3. **You may not edit a bar, a frozen file or a recorded expected value to make a run pass.** If a bar was wrong, add an amendment (date, reason, what changed, what was re-scored) to `docs/PLAN.md` and re-score everything; the old result stays visible.
4. **Frozen files**: `patches/geogram/*`, the hash-pinned sources listed in `lib/geogram/mp24b/FROZEN_SHA256.txt` and the `*_HASHES_BEFORE_RUN.txt` / `GENERATOR_SHA256.txt` records are byte-for-byte pinned; CI checks them. Changing one is a new pre-registration (new name, new record), not an edit.
5. A **development set that has been used to tune is spent**: acceptance needs a fresh set with new seeds, generator written after the freeze, and file hashes recorded before the first run.
6. Aggregate hashes in `ci/EXPECTED_AGGREGATES.txt` are reproduced, not edited. A difference is reported and investigated.

## Process

Open an issue for anything non-trivial. Keep a pull request to one logical change. Commit messages: a short imperative subject, then the reason. `[skip ci]` in a commit message skips CI (GitHub convention); do not use it on a commit that changes code or tests.
CI: `ci.yml` (fast suite on x86_64 and aarch64, macOS non-required, acceptance and recorded-evaluation jobs, lavapipe GPU job); `gpu-t4.yml` is `workflow_dispatch` only. All actions are pinned to major versions with `permissions: contents: read`.
