# Third-party notices and provenance

- Firmware SDK: [Flipper-XFW/Xtreme-Firmware](https://github.com/Flipper-XFW/Xtreme-Firmware), commit `6523edcc65af2fa1a72e0dc3b0bb1f9929744877`, GPL-3.0. The root `LICENSE` is copied from this base. The firmware checkout and its third-party submodules are downloaded by the build instructions and keep their own licenses. No firmware or regional-unlock patch is included in this project.
- The Flipper apps, web interface and project tools in this repository are distributed under GPL-3.0. Historical files retain any existing notices.
- Web font: Inter, SIL Open Font License 1.1. Full attribution and license are preserved in [`web/Inter-LICENSE.txt`](web/Inter-LICENSE.txt), including in the original release packages.
- Reference projects consulted during experiments: [Walkie Talkie catalogue entry](https://lab.flipper.net/apps/walkie_talkie) and [FM transmitter catalogue entry](https://lab.flipper.net/apps/fmtx). Their full source checkouts are not vendored here.
- IMA ADPCM is used as an interoperable block format. Tests compare the local codec implementation with the separately installed FFmpeg decoder; FFmpeg is not bundled.
- Spoken-channel assets were generated with macOS speech synthesis (Samantha, English US). The voice generator is included, along with PDM assets and WAV previews. System speech voices and any applicable media rights are separate from the source-code license. Recorded Bulgarian/English speech is private project test data, not an independently licensed speech dataset.
