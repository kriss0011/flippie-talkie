> Historical two-app documentation. Version 0.5 merges both modes; see the [current controls](../../README.md) and [merge validation](REDESIGN-0.5.md).

Current branding and installation checks are in [RENAME-0.4.md](RENAME-0.4.md). The original names below identify the releases actually tested.

# Validation and evidence boundaries

For the current PMR Connect 0.3 fix and new 16-test suite, see [SMOOTHNESS-0.3.md](SMOOTHNESS-0.3.md). The v0.2 and initial repository-packaging results below are retained as historical evidence.

Original device experiments and reports were produced on 5 November 2026, using the test Flipper on Xtreme API 49.2 and a Motorola T72, principally channel 9. This repository-packaging work did not change or reinstall the device apps.

## PMR Connect 0.2

| Check | Result / evidence |
| --- | --- |
| App build | API 49.2 external-app build passed; source and released FAP included |
| BLE downlink capacity | 99 of approximately 99 expected audio-size diagnostic packets; no receive drops; maximum status gap about 1.09 s. [`ble-capacity.json`](../../validation/connect/ble-capacity.json) |
| BLE uplink | 39 IMA blocks, about 2.09 s nominal block duration; no underflow reported during data flow; write-without-response used. [`ble-stream-result.json`](../../validation/connect/ble-stream-result.json) |
| Missing-audio RF stop | STOP deliberately omitted while heartbeats continued; stopped approximately 0.51 s after final host audio write. End-of-test underruns include intentional starvation and do not imply underflow throughout the speech |
| USB | Mode selection, channel 1/16/9, listen and missing-heartbeat stop passed. [`usb-result.json`](../../validation/connect/usb-result.json) |
| Real received audio | User confirmed hearing Motorola through the Mac browser before the final transport fix |
| Final sustained RF listening / phone audio | Still needs user testing; capacity results are not listening-quality or range measurements |

The original release identity is preserved in [`Build.json`](../../validation/connect/Build.json). Hosted-site commit and FAP SHA are separate identities; a website deployment is not evidence of a successful device install.

## PMR Radio 0.17

- The rollback DSP test compared 125,783 captured samples: saved PCM was bit-identical to v0.16; speaker PWM matched pre-boost v0.12 for all volumes 0–8. Mute/DC silence checks passed. [`rollback-validation.json`](../../validation/radio/rollback-validation.json)
- Installation checksum/readback passed. One SD bench showed a roughly one-second write stall and dropped/gap samples; the repeat three-second bench had zero dropped samples and gaps. The intermittent stall is unresolved. [`installation-and-device-check.json`](../../validation/radio/installation-and-device-check.json)
- User confirmed hearing the rising tone sequence and clear “Testing channel nine” on the Motorola; later reported it worked three floors away. This is user listening feedback, not a calibrated link-budget/range test.
- Additional gain/treble speaker changes were removed at the user's request. No claim of improved RF reception follows from that rollback.

## Repository checks

`npm test` runs eight offline checks:

1. Microphone 44.1 kHz → 6 kHz continuous resampling.
2. Microphone 48 kHz → 6 kHz continuous resampling.
3. C and JavaScript IMA encoders/decoders agree sample for sample.
4. Independent FFmpeg IMA WAV decoder agrees.
5. Parser handles noise and arbitrary BLE/USB splits.
6. Malformed audio blocks are rejected.
7. All 16 PDM assets pass checksum, exact bits and finite timing checks.
8. Rising tone sequence terminates at exactly 5.4 seconds.

`tools/verify_bundle.py` verifies released assets against stored SHA-256 manifests. `tools/build.sh` builds both apps against the pinned SDK without flashing or connecting to hardware. The packaging verification report in [`validation/repository-checks.json`](../../validation/repository-checks.json) distinguishes the checks rerun for this repository from the earlier physical-device reports.
