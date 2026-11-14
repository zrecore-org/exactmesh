# Security policy

exactmesh is research code that reads binary mesh files and runs Boolean operations on them; it has not been hardened against hostile input. Treat mesh files from untrusted sources accordingly
(run in a sandbox, apply resource limits).

## Reporting a vulnerability

Please report suspected vulnerabilities **privately** through GitHub's "Report a vulnerability" (private security advisory) on this repository. Do not open a public issue. Include the input file or a
way to reproduce, the platform and compiler, and what you observed. You will get an acknowledgement within a few working days; fixes and disclosure timing are agreed with the reporter.

## Scope

* In scope: memory-safety or denial-of-service problems in code of this repository (the validator, the pipeline CLI, the mesh reader, the oracle tools, the GPU host programs and shaders).
* Wrong geometric results are correctness bugs, not vulnerabilities: report them as ordinary issues with the input pair (see `CONTRIBUTING.md`).
* Defects in Geogram, Manifold, Boost, Shewchuk's predicates, Vulkan drivers and other third-party software belong to those projects; tell us too if the pinned version matters here.

## Supply chain

Every third-party archive is downloaded from a pinned URL and verified against a sha256 recorded in the repository before it is unpacked (`third_party/fetch.sh`, `ci/geogram_xplat.sh`,
`ci/manifold_xplat.sh`). A changed hash stops the build; the pin is never updated silently. Nothing third-party is vendored. GitHub Actions are pinned to major versions with read-only token permissions.
