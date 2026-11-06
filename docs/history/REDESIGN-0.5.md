# Flippie Talkie 0.5: unified native app

The two previously separate native apps are now internal modes of one FAP. There is one maintained `application.fam`, one app entry point, one SD app binary and one current release package. The browser interface and wire protocol are unchanged.

## Experience

- Native home menu: Listen on Flipper, Browser link, Settings, Quick start.
- Shared settings: all 16 channels, speaker volume 0–8, automatic WAV recording, Bluetooth/USB preference.
- Existing version-1 settings migrate to a checksummed version-2 record; channel, volume and recording choice are retained. New records include transport preference. Writes use a synced temporary file and rename.
- Main radio screens use a clear frequency, large channel number, actual RSSI meter, mode/status and native soft buttons.
- Config uses inverted selection, editable values and a scrollbar where needed. Radio tests are separate explicit actions. Connection help is available on the browser screen.
- Back returns to Home; Back on Home exits. Each mode releases its hardware/transport resources before the next mode starts.
- Both DSP paths remain distinct internally, preserving standalone speaker processing and the earlier Bluetooth/USB sample-rate filtering.

The visual reference is the pinned firmware's [Sub-GHz receiver](https://github.com/Flipper-XFW/Xtreme-Firmware/blob/6523edcc65af2fa1a72e0dc3b0bb1f9929744877/applications/main/subghz/views/receiver.c): monochrome canvas, inverted selection, native soft buttons and receiver information. This is a PMR audio application; it does not add Sub-GHz remote decoding or fabricated signal history.

## Evidence

Screens in `validation/redesign-0.5/before/` and `after/` are actual 128×64 frames retrieved over USB RPC from the connected Flipper, enlarged with nearest-neighbor scaling and an amber display palette. They are not design mockups. The comparison sheets assemble these captured frames.

Offline validation: **17 tests pass**, including codec/FFmpeg compatibility, audio buffering, all 16 voice assets, finite tone timing, receiver gate behavior and new preference migration/corruption/bounds checks.

On-device navigation checks:

1. Changing shared preferences applies channel 10, volume 7 and recording off to standalone mode.
2. OK pauses standalone listening.
3. OK resumes standalone listening.
4. Browser mode picks up channel 10 and USB preference.
5. Original channel 9, volume 8 and recording on are restored through Settings.
6. Returning from standalone to browser mode preserves channel 9 and Bluetooth preference.
7. Preferences survive an app exit/restart.

USB protocol check: handshake, channel 1/16/9 changes, listening enable and automatic pause/error 5 when heartbeats stop. No transmit packets are sent.

Bluetooth check: authenticated connection to the existing paired device, **80/80** synthetic audio-size link-test packets, 11 status packets, zero reported drops/timeouts/failures. This is transport validation; no RF transmission occurs.

Full command/status evidence is in `device-checks.json`, `usb-check.json` and `ble-capacity.json`. Build uses pinned Xtreme firmware API 49.2; FAP link/API checks pass. The missing external-catalogue manifest warning is expected for this SDK checkout.

No new over-the-air voice/tone, RF range, speech quality, active-carrier recording, or iPhone listening claim is made by these UI/merge checks. Existing RF restrictions and watchdogs are retained.

## Delivery

The current install package contains only `sd/apps/Sub-GHz/Flippie_Talkie.fap` and the existing 16 voice assets. The old two release ZIPs and separate Radio FAP are removed from current delivery. Historical experiments and original evidence under `archive/` remain unchanged and are excluded from the new install package.

The connected Flipper's original app binaries and settings were backed up locally before replacement. On-device rollback binaries use `.bak` files under `apps_data/pmr_radio/`, outside the apps menu. `installation.json` records the final binary hash and app-directory inventory. No firmware flash or web deployment is performed.
