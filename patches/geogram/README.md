# Geogram patches

`geogram_mp20.patch` and `geogram_mp22.patch` modify `src/lib/geogram/mesh/mesh_surface_intersection.cpp` of Geogram v1.10.1
(https://github.com/BrunoLevy/geogram, tag v1.10.1, commit bbcacba146b357926b5524111f56cc70c42182a7). They are derivative works of
Geogram and are distributed under Geogram's BSD-3-Clause license, with Geogram's copyright notice, in `LICENSE` of this directory.
They are NOT covered by the Apache-2.0 license of the rest of this repository.

The files are frozen: their sha256 values are recorded in `FROZEN_SHA256_mp20.txt` and `FROZEN_SHA256_mp22.txt` and checked by `ci/geogram_xplat.sh`
before every build. Apply in this order (MP20 first, then MP22) with `patch -p1` from the root of the Geogram source tree.
What each patch fixes is written down in `docs/LIMITS.md`.
