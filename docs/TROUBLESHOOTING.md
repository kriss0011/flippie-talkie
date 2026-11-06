# Troubleshooting

## The Flipper says the app is incompatible

The supplied app targets RogueMaster API 88.23. Other firmware needs a matching build; see [BUILD.md](BUILD.md). The Bluetooth code uses firmware internals, so a different firmware may need source changes.

## My Flipper doesn't appear in the Bluetooth list

- Open **Browser link** on the Flipper first, with **Settings → Connection** set to Bluetooth.
- Use Chrome, or Bluefy on iPhone and iPad, and allow Bluetooth for the page.
- Close the official Flipper mobile app and other browser tabs. Only one app can hold the connection.
- If pairing fails, remove the old pairing from your system's Bluetooth settings and the Flipper, reopen the app and pair again.

## “No reply from the Flipper”

**Browser link** must be open on the Flipper, and **Settings → Connection** must match the page. Changing the connection ends the session, so connect again afterwards.

## The USB port won't open

Close qFlipper, serial terminals and other tabs using the device. Open **Browser link** on the Flipper with Connection set to USB, then connect from Chrome or Edge on a computer.

## “Connection lost, so transmitting stopped”

Keep the page in front and the phone awake, and stay close to the Flipper. Check that the app is still open on the Flipper, then connect again. The page and Flipper app should come from the same release.

## Connected but silent

- Select **Start listening**, and check the page volume and your output device.
- Lower **Squelch**, or drag it fully left to turn it off. “Signal below squelch” means a signal is arriving but is being muted.
- Match the channel on both radios and turn privacy codes off.
- Speak while you press the other radio's transmit button. A silent carrier has no voice to hear.
- Start with the radios about a metre apart, not touching.

## Speech is quiet or noisy

This is the limit of the slope-detector receiver; see [How it works](HOW-IT-WORKS.md). More gain makes speech and noise louder together. Browser playback through good speakers can be easier to follow than the Flipper's speaker.

## The other radio misses my first words

The app already sends half a second of silence first. If a radio still clips words, wait until the button shows **Talking…** before you speak.

## “This channel is busy”

The Flipper can hear a signal on the channel, so it won't transmit. Wait, or pick another channel. Strong local interference can cause this too.

## “TX blocked”

The firmware's region check is stopping the transmission, which is the default for 446 MHz. See [Allow transmitting on PMR446](../README.md#allow-transmitting-on-pmr446), including the legal caution. Listening isn't affected.

## The voice test doesn't work

Copy all 16 `chNN.pdm` files to `apps_data/pmr_radio/voice/`. Each file carries its channel number and a checksum, so renaming another channel's file won't work.

## Recordings are missing or have gaps

- On the Flipper, check **Auto-record** and free space, and let recording finish before you remove the card. SD write stalls have lost samples before, and filtering can't restore them.
- In the browser, recordings stay in that browser only and can be cleared with site data. Download anything you want to keep.

## It works on my Mac but not my phone

`localhost` on a phone means the phone itself, and Bluetooth and the microphone need HTTPS. Use the hosted app, or put `web/` on your own HTTPS host.
