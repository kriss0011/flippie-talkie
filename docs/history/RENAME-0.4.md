> Historical two-app documentation. Version 0.5 merges both modes; see the [current controls](../../README.md) and [merge validation](REDESIGN-0.5.md).

# Flippie Talkie: rename and upgrade

The project is now **Flippie Talkie**. This release changes branding, app/package names and the web address. It keeps the receive-audio improvements from version 0.3 and the standalone speaker behavior from version 0.17.

| Component | Current name / location |
| --- | --- |
| Private repository | https://github.com/kriss0011/flippie-talkie |
| Private web interface | https://flippie-talkie.kriss0011.chatgpt.site |
| Browser-linked Flipper app | **Flippie Talkie 0.4** — `Flippie_Talkie.fap` |
| Standalone Flipper app | **Flippie Talkie Radio 0.18** — `Flippie_Talkie_Radio.fap` |

## Upgrade

Follow the [installation instructions](../../README.md#install-on-flipper). Both apps remain in **Apps → Sub-GHz**. After installing the new filenames, move the old app binaries out of that menu directory to avoid duplicates. Do not remove the app data directories.

Open the new web address and sign in as the owner. A new website origin may need fresh Bluetooth and microphone permission. Choose the same Flipper, your Flipper, when connecting. The Flipper's device name and pairing identity have not changed. Reload until the footer reads **Flippie Talkie 0.4**.

## Data and protocol compatibility

Existing settings, voice assets and recordings keep their original locations:

- `SD/apps_data/pmr_radio/settings.bin`
- `SD/apps_data/pmr_radio/voice/chNN.pdm`
- `SD/apps_data/pmr_voice/*.wav`

The internal source/app identifiers `pmr_connect` and `pmr_capture`, CLI commands, Bluetooth UUIDs, and USB handshake `PMRCONNECT1` are retained for compatibility. Protocol version remains **3**; it is distinct from app version **0.4**. No frequency, DSP, gain, playback-buffer, transport or transmit behavior changed in this release.

Original experiment files, reports and release names remain historical evidence. Previous packages and the old interface screenshot are preserved in the original release archive (no longer in this repository). Their original manifest uses the original package paths; the current release manifest inventories their new archive locations and unchanged checksums.

## Validation

Both apps built successfully against Xtreme API 49.2. All 16 offline tests passed. Both renamed apps were installed, verified by checksum and launched on the test Flipper. All 24 existing WAV recordings and the saved settings retained their checksums. The 17 older experimental app binaries were moved out of the app menu into `SD/apps_data/flippie_talkie/previous_apps/`, with checksums verified. Device installation and checksum verification are recorded in [`validation/rename-0.4/`](../../validation/rename-0.4/).

The web interface was published with the existing owner-only audience. Visual browser QA was unavailable because the Mac was locked. This rename does not establish new RF/audio quality results: the sustained iPhone/Bluefy smoothness test remains pending.
