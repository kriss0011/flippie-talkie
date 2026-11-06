# RogueMaster compatibility: Flippie Talkie 0.6

The API 49.2 binary is rejected by RogueMaster RM1024-1347-392a65c3 with `ApiTooOld`.
Version 0.6 targets that release's API 88.23 and its pinned source commit
`e1837ff6a1a7d5c91a0782343a98e2e600d62da7`.

The CLI commands now register with `CliRegistry` and read/write the command's
`PipeSide`. USB polling reads only available bytes, preserving status updates,
heartbeats and disconnect handling when microphone data is absent.

Browser Bluetooth audio uses an app-owned serial-service profile. It keeps the
firmware's secure pairing configuration and the existing service/characteristic
UUIDs, while keeping firmware RPC separate. Exiting browser mode restores the
default firmware Bluetooth profile before the app can unload.

The DSP, audio codec, PMR channel map, voice file format, saved settings format,
recording paths, firmware TX checks and transmission watchdogs are unchanged.
Existing voice assets and settings do not need conversion.

## Validation on the test Flipper

- The pinned RogueMaster build passes SDK/API 88.23 and FAP import checks.
- The installed binary launches successfully instead of `ApiTooOld`.
- Seven on-device UI/preferences checks pass: shared settings, standalone
  pause/resume, USB-mode preferences, returning between modes, and persistence
  across exit/restart. Actual USB screen captures are in
  `validation/roguemaster-0.6/after/`.
- USB handshake, channels 1/16/9, listening enable and automatic pause on lost
  heartbeats pass. The existing wire protocol is retained.
- With the user's browser already connected over Bluetooth, an eight-second
  synthetic 80-packet probe produced 93 acknowledged indications including
  status/other traffic, with no additional BLE timeouts or failures. This is a
  firmware indication-delivery check; it does not count application-decoded
  browser audio packets.
- All 17 host tests pass, including independent FFmpeg codec compatibility.
- Firmware TX restrictions are retained. The user's transmit attempt reported
  error 2 (firmware block), with zero successful transmit tests. No automated
  RF transmission, new range test or over-the-air speech-quality claim is made.

The old installed FAP and the original nine-byte settings record were copied
and hash-verified before replacement. The old FAP also remains outside the app
menu as `apps_data/pmr_radio/pre-0.6-api49.fap.bak`. User channel changes made
while testing the browser are left in place.

## Transmit-block setting

RogueMaster exposes `CFW Settings -> Protocols -> SubGHz Bypass Region Lock`.
Changing that setting requires the firmware's confirmation and a reboot.
446 MHz is within the normal supported range, so extending frequency bands is
not required. The app does not change either firmware setting. Use only on
frequencies where you are authorized to transmit.
