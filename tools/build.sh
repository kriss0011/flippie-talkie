#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
sdk_path="${PMR_SDK_DIR:-$repo_root/.cache/roguemaster-firmware}"
firmware_revision=e1837ff6a1a7d5c91a0782343a98e2e600d62da7
if [[ ! -f "$sdk_path/fbt" ]]; then
    echo 'Run bash tools/setup_sdk.sh first (or set PMR_SDK_DIR).' >&2
    exit 1
fi
sdk_path="$(cd "$sdk_path" && pwd)"
if [[ "$(git -C "$sdk_path" rev-parse HEAD)" != "$firmware_revision" ]]; then
    echo "This build is pinned to firmware $firmware_revision." >&2
    exit 1
fi
mkdir -p "$repo_root/build/sd/apps/Sub-GHz"
app=flippie_talkie
target="$sdk_path/applications_user/$app"
if [[ -d "$target" ]] && ! diff -qr "$repo_root/firmware/$app" "$target" >/dev/null; then
    echo "Existing $target differs. Save/move it first, or use a fresh SDK." >&2
    exit 1
fi
mkdir -p "$target"
cp "$repo_root/firmware/$app/"* "$target/"
(cd "$sdk_path" && FBT_NO_SYNC=1 ./fbt "--options=$repo_root/tools/fbt_roguemaster.py" build "APPSRC=applications_user/$app")
cp "$sdk_path/build/f7-firmware-C/.extapps/$app.fap" "$repo_root/build/sd/apps/Sub-GHz/Flippie_Talkie.fap"
mkdir -p "$repo_root/build/sd/apps_data/pmr_radio"
cp -R "$repo_root/sd/apps_data/pmr_radio/voice" "$repo_root/build/sd/apps_data/pmr_radio/"
echo "Built Flippie Talkie. Install files from $repo_root/build/sd/"
