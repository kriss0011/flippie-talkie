# App 0.7: API 88 voice-transmit timing correction

The user reported noise on the Motorola where channel-test speech had previously been clear. Both standalone and USB remote transmit paths reached TX state, so that alone did not establish audible speech reception.

The older API 49 firmware writes callback durations directly to the timer ARR register; its timer runs for ARR + 1 ticks. The newer API 88 firmware merges pulse runs and subtracts one before writing ARR. Both app voice callbacks still applied the old one-tick subtraction, causing a second subtraction on the newer firmware.

App 0.7 returns unadjusted microseconds from both `tx_voice_yield` (stored channel prompts) and `tx_yield` (browser/remote PCM). API 88 owns the timer conversion. Tone-test callbacks already returned unadjusted durations. Frequency, RF power, deviation, receive settings, voice assets, browser protocol, recording locations and firmware permission checks are unchanged.

## Evidence

- A regression test compiles the actual app callback bodies and verifies all 16 voice prompt durations plus exact 48 kHz timing for remote zero-PCM PDM. The previous bodies fail this test; the corrected ones pass.
- All 33 host tests pass. The API 88.23 FAP build and import checks pass.
- Installed app 0.7 and checked its hash on the test Flipper. Previous app retained at `SD/apps_data/pmr_radio/Flippie_Talkie_0.6_before_tx_timing_fix.fap.bak`. The settings checksum stayed unchanged.
- Before correction, the 2.039625-second stored prompt completed in 1.940 seconds; after correction it completed in 2.044 seconds, including polling/tick granularity.
- Two actual USB remote sends of 39 audio blocks each reached TX state and finished with zero audio underruns, zero dropped blocks and no device error. The previous remote attempts had 252 and 222 underrun samples respectively.
- Bluetooth mode was restored on channel 9 after the tests.

Reports are in [`validation/tx-timing-0.7/`](../../validation/tx-timing-0.7/). These checks establish the timing correction and successful device handling. The Motorola listening result is pending; intelligible over-the-air speech is not claimed until the user confirms it.

This is an app update for the installed RogueMaster API 88.23 firmware. It does not flash system firmware. The Web 0.7 browser changes are separate and remain compatible.
