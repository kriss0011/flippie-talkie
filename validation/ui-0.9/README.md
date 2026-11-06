# 0.9 interface checks

- `npm test`: 38 host tests pass.
- RogueMaster API 88.23 build and APPCHK pass.
- Flipper screens in `docs/images/` are real USB captures of this build, run as a temporary copy on the test device. Short key presses only; no RF transmission.
- `usb-check.json`: USB handshake, channel 1/16/9 changes, listen enable and automatic pause on missing heartbeats. No transmit packets were sent. Settings checksum unchanged.
- Web app: connect, channel change and disconnect ran against a simulated Flipper at 320, 390, 768 and 1100 px with no horizontal overflow.

Not checked for 0.9: the web app with a real Flipper, Bluetooth, RF transmit, RF receive quality and phone hardware. The radio, audio and protocol code changed only in formatting and on-screen text.
