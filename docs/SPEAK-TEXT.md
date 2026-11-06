# Speak text: typed messages spoken over the radio

Type a message on the Flipper and it is spoken on the channel, with no phone or computer. The voice is synthesized on the device. It is robotic: good enough for short, simple messages, not for unusual names or fast reading.

## Use

Open **Speak text** from the main menu.

| Row | What it does |
| --- | --- |
| Message | Opens the keyboard, up to 160 characters. The old text starts selected: the first key replaces it, Save keeps it. `_` is a word gap. |
| Send on radio | Transmits the message. OK or Back stops at once. |
| Hear on Flipper | Plays the same audio on the Flipper speaker. Nothing is transmitted. Uses the Speaker volume from Settings. |
| Repeats | 1–99 sends. |
| Pause | 1–60 s between sends, leaving the channel free for replies. |
| Speed, Pitch | Normal and Mid are the clearest. |
| Channel | The shared PMR channel setting. |

Messages are in English. Digits are read one at a time, and words with no vowels are spelled out. Settings and the message are saved in `apps_data/pmr_radio/speak.txt`, which can also be edited on a computer.

## Radio behaviour

Each send checks that the channel is clear first and waits while it is busy, using the same noise-floor rule as the browser link. A send uses the same preset, 500 ms lead-in and 60-second limit as browser push-to-talk. Audio is generated while it transmits, so nothing is stored.

## How it works

`pmr_tts.c` turns text into phonemes with the rules in `pmr_tts_rules.h`, then drives a formant synthesizer at 12 kHz, decimated to the radio's 6 kHz. Output has the same pre-emphasis and 0.85 peak as the stored channel prompts. `pmr_speak_core.h` holds the settings and PDM modulator, with no Flipper dependencies, so host tests compile the real code.

**Speech must stay in its plugin.** `application.fam` builds `pmr_speak.c` and `pmr_tts.c` as an embedded plugin, loaded only while Speak text is open. The loader unpacks it to `apps_assets/flippie_talkie/plugins/` on first launch, so installing is still one FAP. Listen allocates about 85 KB when a signal arrives, and with speech linked into the main app that allocation failed and rebooted the Flipper. Opening Listen does not reveal this; only starting a recording does. A test now guards this split.

To hear a message on a computer:

```sh
cc -std=c11 -O2 -I firmware/flippie_talkie tools/tts_preview.c firmware/flippie_talkie/pmr_tts.c -lm -o build/tts_preview
build/tts_preview "Hello, this is a test." hello.wav
```

## Validation

Measured on the connected Flipper, RogueMaster API 88.23, channel 9.

| Check | Result |
| --- | --- |
| Free heap in Listen | 97,624 bytes, against 97,936 before speech and 75,472 with speech in the main app |
| Recording with `sdtest`, before and after using Speak text | Saved, 0 dropped; the speech-in-main-app build crashed and rebooted |
| Transmission | 6,893 ms against 6,890 ms predicted, 0 underruns |
| Three repeats, 2 s pause | All sent, each within 10 ms of the prediction |
| Stop during transmission | Ended at 1,372 ms |
| Hear on Flipper | 23,132 samples delivered, PWM 29–996 of 1023, PWM confirmed on the speaker pin |
| Synthesis load | About 13% of the CPU |

Clarity was scored with Whisper `base.en` through a simulated receiver (de-emphasis, 300–2850 Hz). This is only a proxy for a person listening.

| Voice | Word errors |
| --- | --- |
| macOS Samantha, for reference, 12 sentences | 6.4% |
| First working synthesizer, 12 sentences | 80.8% |
| Final, 18 sentences | 20.0% |
| Final, 8 sentences built around "speak" and similar words | 15.4% (30.8% before the /s/ cluster fix) |

The user confirmed on the device that Listen on Flipper works with a live transmission, that Hear on Flipper is audible, and that Send on radio works.

Not yet measured: the busy-channel wait against a real second transmitter, range, and radiated power. [`flippie-talkie-speaking.wav`](../validation/speak-text/flippie-talkie-speaking.wav) is a sample of the 6 kHz audio that is transmitted.
