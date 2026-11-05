# Troubleshooting

## Flipper says the app API is incompatible

The supplied binaries target Xtreme API 49.2. Check your actual firmware/API. Rebuild/port for that firmware rather than flashing an unrelated firmware just to make this binary load. The BLE integration uses this older SDK's serial profile internals; a newer build may require source changes.

## Flipper does not appear, or pairing fails

Open Flippie Talkie on Flipper and select Bluetooth with OK. Use a supported browser with Bluetooth permission. Disconnect other browser tabs and the official Flipper mobile app; the device supports one central connection here. Select the device's actual name (Dobby on the tested unit). Pairing is initiated by a secure characteristic read; when a system pairing prompt appears, enter the current code displayed on Flipper. A previously paired device may reconnect without a code. If pairing state is stale, disconnect, remove the old pairing in system settings, reopen the app and pair again.

## “Connection lost. Transmission has stopped.”

The interface disables talk/listen when contact is lost. Open the app again if it exited, confirm the selected transport matches, then reconnect. Keep the page foreground and the phone awake. Do not run two clients. Version 0.3 uses 3.2 kHz Bluetooth downlink audio, prioritizes status, and buffers short timing variations without restarting audible playback. Ensure the web interface and FAP are both from this release.

## Connected but silent

- Select Start listening. Check browser/system speaker volume and output device, including any Bluetooth headphones.
- Match the channel on both devices; on the Motorola T72 turn privacy/subcodes off. Channel 9 is 446.10625 MHz.
- Press the Motorola transmit button and speak; a silent carrier button press contains no voice.
- Start a practical test around a metre apart, not touching. Compare with a normal radio to separate RF issues from browser playback.
- Allow microphone access before browser push-to-talk. A microphone level meter only verifies the browser input, not successful RF output.
- Flippie Talkie uses the computer/phone speaker. For the Flipper's own speaker use Flippie Talkie Radio and set volume above zero.

## Speech is quiet or noisy

The receiver is an RSSI slope-detector experiment. Its noise and distortion are real limitations; extra gain makes both speech and noise louder. The standalone 0.17 app restores the original linear speaker mapping. Use the browser/computer speaker for greater playback volume. The 3.2 kHz Bluetooth / 4 kHz USB downlink also limits voice bandwidth. Better speaker volume is not evidence of better RF sensitivity.

## USB cannot open the port

Close qFlipper, any serial terminal, and other browser tabs using the device. Open Flippie Talkie manually, select USB on Flipper, then USB in desktop Chrome/Edge. Reconnect after changing modes. If the app is not open, its `pmr_connect usb` command does not exist.

## Spoken tests are missing

Use Flippie Talkie Radio, not Flippie Talkie. Install every `chNN.pdm` into `SD/apps_data/pmr_radio/voice/`, select a channel, then hold OK on the main screen. The file header includes the channel number and checksum, so simply renaming another channel's asset will not make a valid replacement. Run the offline asset tests after regenerating prompts.

## Recordings have gaps or fail to save

Flippie Talkie Radio saves WAV files under `SD/apps_data/pmr_voice/`; Flippie Talkie does not save recordings. Check the menu's recording setting, card free space and that the app finishes recording before you copy a file. A slow/occasionally stalled SD write was observed and is not proven resolved. The validation report records one failure followed by a passing repeat; don't assume missing audio can be restored by filtering. Preserve original files before repair or analysis. Incorrect device time affects filenames, not the nominal audio sample rate.

## Local page works on Mac but not phone

`localhost` on a phone refers to the phone itself. Use the hosted HTTPS interface, or deploy `web/` to your own HTTPS host. Plain LAN HTTP is insufficient. iOS requires a browser exposing Web Bluetooth (Bluefy is the candidate); the user confirmed basic operation on iPhone, but sustained smooth playback with v0.3 still needs a listening test. A site's sign-in/hosting access and its permission to use nearby Bluetooth hardware are separate steps.
