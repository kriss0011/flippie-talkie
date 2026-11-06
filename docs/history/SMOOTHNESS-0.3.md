> Historical two-app documentation. Version 0.5 merges both modes; see the [current controls](../../README.md) and [merge validation](REDESIGN-0.5.md).

# PMR Connect 0.3 — short receive-audio cuts

The user confirmed that the interface works in Bluefy on iPhone, but received audio briefly cut out every few seconds. This update targets continuity, not increased RF range or louder gain.

## Findings

The previously installed app reported an aggregate receive-drop counter of 233, 749 BLE sends and a maximum measured send wait of 202 ms. The old counter combines multiple failure paths, so that number alone cannot distinguish transport packets from sampling drops. No SPI read errors were reported.

The old browser scheduled individual audio sources with 100 ms initial headroom. Whenever the next source was less than 15 ms ahead, or more than 350 ms ahead, it stopped all audible/queued sources and started again. An ordinary late packet could therefore become an additional audible cut even when usable samples remained queued.

The receiver also paused sampling, retuned to channel centre and back, and reset its DSP whenever the off-centre RSSI stayed below a fixed threshold. A steady but weak/suppressed off-centre level could trigger that repeatedly, at 0.6/1.2/2.4-second intervals. No trace from the user's phone session proves how much of the reported cutting came from this path versus Bluetooth/playback.

## Changes

- Continuous AudioWorklet playback queue, approximately 240 ms target buffering, with gentle clock-drift correction. Normal delivery jitter no longer stops and clears current playback. Unexpected missing blocks are counted and faded to silence, not replaced with generated speech.
- Bluetooth receive rate reduced from 4000 to 3200 samples/second: 20% fewer audio blocks, with a matching narrower receive filter. USB remains 4000 samples/second; microphone transmission remains 6000. This trades some upper voice bandwidth and a little startup delay for transport headroom.
- BLE status remains prioritized, with two-second periodic status during reception and immediate state updates. The indication acknowledgement wait allows up to 500 ms instead of treating every delay beyond 200 ms as a failed packet.
- Receiver release detection learns the level of a carrier already confirmed at channel centre. Stable carriers above or below the measured noise level no longer require repeated centre checks. A real level change still requests a check after a 600 ms hold. When carrier/noise levels are indistinguishable, an eight-second fallback check remains; perfect release detection is not possible from that ambiguous measurement alone.
- DSP and fractional sample-clock state survive a necessary check/resume. They reset for a genuinely new receive.
- Separate CLI counters identify sample overflow, outgoing queue overflow, BLE timeout/failure and centre rechecks. Browser diagnostics report playback starvation and queued duration.

The frame layout is unchanged, but status reports protocol 3 and the transport-specific receive rate. The new web page also accepts the previous protocol-2 / 4 kHz app. An old page must be refreshed to work with the new FAP. The footer reads **PMR Connect 0.3**.

## Verification performed

- Both FAP builds passed against the pinned API 49.2 SDK. Only PMR Connect changed on the device.
- New app installed and its checksum read back. Previous binary retained at `/ext/apps_data/pmr_radio/PMR_Connect_v02_before_v03.fap`.
- All 16 offline tests passed, including repeated 180 ms delivery stalls at both receive rates and 44.1/48 kHz output, two-minute ±0.3% clock mismatch, missing/duplicate packets, squelch release/hold behavior and the existing transmit codec/voice/tone checks.
- Native Mac Bluetooth test delivered **80/80** diagnostic audio-size packets with **zero drops, queue overflow, indication timeouts or send failures**. Maximum measured indication wait was 124 ms in that run. This tests the actual transport, not the iPhone audio output or RF sensitivity.
- USB channel 1/16/9, listening and missing-heartbeat stop checks passed, followed by return to Bluetooth channel 9. No RF transmission was needed for this change's bench checks.
- The Mac was locked during browser verification, so a new visual/real-browser playback check was unavailable. The pure playback engine tests do not substitute for an actual iPhone listening test.

Original reports are in [`validation/connect-v0.3/`](../../validation/connect-v0.3/). Existing v0.2 reports remain historical evidence. The next acceptance check is a continuous 30-second Motorola transmission heard in Bluefy with the page foreground, followed by releasing PTT to check that reception returns to idle. The user has not yet confirmed smoothness of this new version.
