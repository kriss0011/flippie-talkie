# Third-party notices and provenance

- Firmware SDK: [RogueMaster/flipperzero-firmware-wPlugins](https://github.com/RogueMaster/flipperzero-firmware-wPlugins), commit `e1837ff6a1a7d5c91a0782343a98e2e600d62da7`, GPL-3.0. The root `LICENSE` is copied from this base. The firmware checkout and its third-party submodules are downloaded by the build instructions and keep their own licenses. No firmware or regional-unlock patch is included in this project.
- The Flipper apps, web interface and project tools in this repository are distributed under GPL-3.0. 
- Web font: Inter, SIL Open Font License 1.1. Full attribution and license are preserved in [`web/Inter-LICENSE.txt`](web/Inter-LICENSE.txt).
- Reference projects consulted during experiments: [Walkie Talkie catalogue entry](https://lab.flipper.net/apps/walkie_talkie) and [FM transmitter catalogue entry](https://lab.flipper.net/apps/fmtx). Their full source checkouts are not vendored here.
- IMA ADPCM is used as an interoperable block format. Tests compare the local codec implementation with the separately installed FFmpeg decoder; FFmpeg is not bundled.
- Spoken-channel assets were generated with macOS speech synthesis (Samantha, English US). The voice generator is included, along with PDM assets and WAV previews. System speech voices and any applicable media rights are separate from the source-code license. Private test recordings are not part of this repository.
