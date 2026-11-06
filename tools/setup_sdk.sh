#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
sdk_path="${PMR_SDK_DIR:-$repo_root/.cache/roguemaster-firmware}"
firmware_revision=e1837ff6a1a7d5c91a0782343a98e2e600d62da7
if [[ ! -d "$sdk_path" ]]; then
    mkdir -p "$(dirname "$sdk_path")"
    git clone --filter=blob:none --no-checkout https://github.com/RogueMaster/flipperzero-firmware-wPlugins.git "$sdk_path"
    git -C "$sdk_path" checkout --detach "$firmware_revision"
fi
if [[ "$(git -C "$sdk_path" rev-parse HEAD)" != "$firmware_revision" ]]; then
    echo "SDK must be at $firmware_revision; existing checkout was left unchanged." >&2
    exit 1
fi
# External catalogue apps are not needed for either FAP. Initialize the SDK's
# pinned libraries and protocol assets, avoiding the unrelated app catalogue.
git -C "$sdk_path" submodule update --init --recursive --depth 1 --jobs 8 -- assets/protobuf lib
echo "SDK ready: $sdk_path"
