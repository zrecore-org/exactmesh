#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Fetches Shewchuk's predicates.c into $EXACTMESH_DEPS/shewchuk (default <repo>/build/deps) and verifies it against oracle/SHA256SUMS.
# Nothing fetched is ever committed (build/ is git-ignored). Usually called through third_party/fetch.sh.
set -e
# macOS has shasum, most Linux images have sha256sum; both print "hash  path"
SHA256="shasum -a 256"; command -v sha256sum >/dev/null && SHA256=sha256sum
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${DEPS:-${EXACTMESH_DEPS:-$HERE/../build/deps}}"
mkdir -p "$DEST/shewchuk"
if [ ! -f "$DEST/shewchuk/predicates.c" ]; then
  curl -fsSL -o "$DEST/shewchuk/predicates.c" "https://www.cs.cmu.edu/afs/cs/project/quake/public/code/predicates.c"
fi
cd "$DEST"
grep -v '^#' "$HERE/SHA256SUMS" | $SHA256 -c -
