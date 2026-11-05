# Historical experiments and recordings

`archive/` preserves the original radio-project files, including earlier source versions, raw captures, complete decoded recordings, calibration sounds, analysis, old releases and bench results. Files were copied unchanged; `archive/manifest.json` records their SHA-256 hashes. Absolute Mac paths and device names in these historical reports describe the original run, not paths that must exist on a new computer.

## Where to look

| Area | Contents |
| --- | --- |
| `archive/outputs/t72-channel9-test*.pmr` | First raw Motorola channel-9 captures |
| `archive/outputs/T72-*`, `test1-audio/`, `test2-audio/`, `test3-audio/` | Early complete audio alternatives, including weak/unsuccessful reconstructions |
| `archive/outputs/calibration/`, `receiver-calibration/` | Generated tones and receiver calibration experiments |
| `archive/outputs/receiver-v10/` | Later timestamped USB captures, settings and complete decoded speech |
| `archive/outputs/one-metre-loud-test/`, `additional-range-tests/`, `two-room-test/` | User distance/loudness tests |
| `archive/outputs/progressive-noise-diagnosis/`, `fixed-tuning-speech-test/` | Investigation of periodic/progressive degradation |
| `archive/outputs/pmr-listen/`, `pmr-radio/` | Standalone speaker/SD work and earlier PMR Radio package |
| `archive/work/pmr_capture_v*.c`, `pmr_dsp_v*.h` | Earlier capture and speaker implementation snapshots |
| `archive/work/*bench*/` | Synthetic/idle/hardware bench data |
| `archive/work/pmr-connect/` | Original Connect host tools, web source, tests and BLE/USB validation data |
| `assets/voice-preview/` | All 16 current spoken channel test previews |
| `sd/` | Current ready-to-install files; use these for installation |

ZIP packages inside the archive are the original delivered bundles and can duplicate neighboring recordings/source. They are retained intentionally. The archive includes private test speech; keep the repository private unless its content is reviewed for wider sharing.

## Work with a raw recording

Prefer existing `*-full.wav` files if the goal is listening. They preserve the captured duration; filtering is not proof of recovered speech. Some early recordings contain mostly noise and are retained as negative results.

For later **PMRCAP3** captures, the portable tool is:

```sh
python tools/pmr_audio.py \
  archive/outputs/receiver-v10/recordings/capture-20261105T153546.012816Z.pmr \
  --out local/decoded-example
```

Keep the corresponding `.json` beside the raw `.pmr`; the decoder uses it to check capture integrity and interpret metadata. Outputs include `reference-full.wav`, `filtered-full.wav`, `cleaned-full.wav` and `analysis.json`. Original captures are not overwritten. Very early formats used different headers/sample layouts: use their matching archived decoder or delivered ZIP, not the PMRCAP3 decoder.

## Historical scripts

These are research scripts, not a supported automation package. Several run actions immediately when invoked, use the original `Dobby` USB/BLE identity, expect the original `work/` + `outputs/` layout, or refer to the downloaded firmware SDK. Installer and transmission scripts are especially unsuitable for blind bulk execution. The original layout is retained under `archive/`; paths such as `work/xtreme-firmware` would still need a compatible SDK and a matching historical app version.

For new development use `firmware/`, `web/`, `tools/` and `tests/` at the repository root. To reproduce an old result, inspect that script's inputs/paths, copy it into a scratch environment, and adapt device paths explicitly. `tools/flipper_cli.py` provides portable USB port selection for new tools. Modern BLE/USB tests should use the protocol guide and final v0.2 rates rather than earlier assumptions.

## Why the architecture changed

Early digital/edge captures did not yield usable speech. RSSI slope demodulation eventually recovered recognizable voice, but required attention to sample timing, filter state and RF tuning. The archive records the calibration and periodic-noise investigations; it does not establish that all reception noise has a single cause.

Standalone recording moved to full received transmissions and WAV output. Stronger nonlinear speaker gain and a treble path made noise objectionable; v0.17 returned to the earlier linear speaker mapping. Spoken channel prompts and tone tests produced clear transmissions in user tests.

PMR Connect moved microphone and speaker duties to the host. BLE pairing needed a secure read before serial traffic, status needed priority over audio, and the downlink had to drop from 6 kHz to 4 kHz to fit the measured indication throughput. Uplink writes without response allowed continuous 6 kHz microphone data. These fixes address transport behavior; they do not turn the CC1101 into a conventional high-quality FM receiver.

## Scope of this archive

Included: project source, host scripts, generated voice assets, calibration tracks, full recordings, build/release artifacts and result reports. Not included: unrelated personal Flipper NFC/RFID/Sub-GHz backups, downloaded upstream firmware/app repositories, compiler/toolchain caches, Python environments, credentials, private hosting configuration or transient connection logs. Upstream repositories and the exact SDK commit are documented for retrieval rather than duplicating gigabytes of external code.
