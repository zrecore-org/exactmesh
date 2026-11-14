#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Fetches the Thingi10K raw archive (9.6 GB) into $THINGI_DIR (default build/research/thingi10k_raw; build/ is git-ignored and should be on a large volume) and verifies size and sha256 against THINGI.pin.
# Never committed. Each model has its own licence (the dataset says "Each thing in the dataset has its own license"): nothing is redistributed by this repository, and results are recorded by model id only.
# Usage: research/realmesh/fetch_thingi.sh --yes-download-9GB
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
[ "$1" = "--yes-download-9GB" ] || { echo "refusing: this downloads 9.6 GB. Re-run with --yes-download-9GB"; exit 2; }
. "$HERE/THINGI.pin"
DEST="${THINGI_DIR:-$ROOT/build/research/thingi10k_raw}"; mkdir -p "$DEST"
[ -f "$DEST/Thingi10K.tar.gz" ] || curl -fL -o "$DEST/Thingi10K.tar.gz" "$url"
got=$(wc -c < "$DEST/Thingi10K.tar.gz" | tr -d ' ')
[ "$got" = "$size" ] || { echo "size mismatch: got $got, pinned $size"; exit 1; }
echo "$sha256  $DEST/Thingi10K.tar.gz" | { sha256sum -c - 2>/dev/null || shasum -a 256 -c -; }
tar -xzf "$DEST/Thingi10K.tar.gz" -C "$DEST"
echo "extracted into $DEST"
