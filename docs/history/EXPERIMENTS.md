# Experiment history

Flippie Talkie grew out of a series of bench experiments between a Flipper Zero and a Motorola T72. The raw captures, recordings and early scripts contain private test speech and are not published in this repository. This page keeps what was learned from them.

## Why the architecture changed

Early digital/edge captures did not yield usable speech. RSSI slope demodulation eventually recovered recognizable voice, but required attention to sample timing, filter state and RF tuning. The archive records the calibration and periodic-noise investigations; it does not establish that all reception noise has a single cause.

Standalone recording moved to full received transmissions and WAV output. Stronger nonlinear speaker gain and a treble path made noise objectionable; v0.17 returned to the earlier linear speaker mapping. Spoken channel prompts and tone tests produced clear transmissions in user tests.

PMR Connect moved microphone and speaker duties to the host. BLE pairing needed a secure read before serial traffic, status needed priority over audio, and the downlink had to drop from 6 kHz to 4 kHz to fit the measured indication throughput. Uplink writes without response allowed continuous 6 kHz microphone data. These fixes address transport behavior; they do not turn the CC1101 into a conventional high-quality FM receiver.

Scripts for reading the later **PMRCAP3** capture format remain in `tools/pmr_audio.py`.
