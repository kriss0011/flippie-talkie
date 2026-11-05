#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
sdk_path="${PMR_SDK_DIR:-$repo_root/.cache/xtreme-firmware}"
firmware_revision=6523edcc65af2fa1a72e0dc3b0bb1f9929744877
if [[ ! -d "$sdk_path" ]]; then
    mkdir -p "$(dirname "$sdk_path")"
    git clone --no-checkout https://github.com/Flipper-XFW/Xtreme-Firmware.git "$sdk_path"
    git -C "$sdk_path" checkout --detach "$firmware_revision"
fi
if [[ "$(git -C "$sdk_path" rev-parse HEAD)" != "$firmware_revision" ]]; then
    echo "SDK must be at $firmware_revision; existing checkout was left unchanged." >&2
    exit 1
fi
# External catalogue apps are not needed for either FAP. Initialize the SDK's
# pinned libraries and protocol assets, avoiding the unrelated app catalogue.
git -C "$sdk_path" submodule update --init --recursive -- assets/protobuf lib
echo "SDK ready: $sdk_path"
