#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
sdk_path="${PMR_SDK_DIR:-$repo_root/.cache/xtreme-firmware}"
firmware_revision=6523edcc65af2fa1a72e0dc3b0bb1f9929744877
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
for app in pmr_capture pmr_connect; do
    target="$sdk_path/applications_user/$app"
    # Refuse to clobber a developer's modified app in an existing SDK checkout.
    if [[ -d "$target" ]] && ! diff -qr "$repo_root/firmware/$app" "$target" >/dev/null; then
        echo "Existing $target differs. Save/move it first, or use a fresh SDK." >&2
        exit 1
    fi
    mkdir -p "$target"
    cp "$repo_root/firmware/$app/"* "$target/"
    (cd "$sdk_path" && FBT_NO_SYNC=1 ./fbt build "APPSRC=applications_user/$app")
    filename=Flippie_Talkie.fap
    if [[ "$app" == pmr_capture ]]; then filename=Flippie_Talkie_Radio.fap; fi
    cp "$sdk_path/build/f7-firmware-C/.extapps/$app.fap" "$repo_root/build/sd/apps/Sub-GHz/$filename"
done
mkdir -p "$repo_root/build/sd/apps_data/pmr_radio"
cp -R "$repo_root/sd/apps_data/pmr_radio/voice" "$repo_root/build/sd/apps_data/pmr_radio/"
echo "Built both apps. Install files from $repo_root/build/sd/"
