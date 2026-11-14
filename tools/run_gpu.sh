#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Runs a built GPU program from the repository root with the Vulkan driver configured (the programs read build/shaders/*.spv relative to the working directory).
#   tools/run_gpu.sh mp10_lbvh device 0.1         tools/with_gpu_lock.sh tools/run_gpu.sh mp09_orient_filter device 400000   (take the shared GPU lock first on a shared machine)
# On macOS the MoltenVK ICD of the Vulkan SDK is used unless VK_ICD_FILENAMES is set; on Linux the system loader picks the driver (lavapipe in CI).
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"
if [ "$(uname -s)" = Darwin ]; then export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-$HOME/VulkanSDK/1.3.283.0/macOS/share/vulkan/icd.d/MoltenVK_icd.json}"; fi
cd "$ROOT"
exec "$ROOT/build/mesh/$1" "${@:2}"
