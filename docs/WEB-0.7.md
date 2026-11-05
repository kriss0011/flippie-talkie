# Flippie Talkie Web 0.7

Browser-only update. No Flipper flash or installation is required. The existing protocol 2/3 Bluetooth and USB transports remain compatible, including the newer native Flippie Talkie interface.

Open https://flippie-talkie.kriss0011.chatgpt.site and reload until the footer reads **Flippie Talkie Web 0.7**. The website is available to anyone with the link; the GitHub repository remains private. Each visitor connects their own nearby Flipper through their browser. Audio and recordings are not uploaded to this website.

## Squelch

Use **Squelch** below Speaker. Start at the default **−95 dBm**, raise it until unwanted weak static stops, and lower it if it suppresses a wanted transmission. Drag all the way left for **Off**. The setting is saved separately for each of the 16 channels in this browser.

This is a browser receive gate based on the signal strength the Flipper reports. It uses 4 dB hysteresis and a 450 ms hold to avoid rapid toggling. It cannot distinguish strong interference from a wanted carrier, clean noise out of an admitted signal, or open reception that the firmware has already suppressed. Status updates constrain its response speed. The current device receiver and gains are unchanged.

Notices have a fixed-height area, and microphone status copy has fixed space, so a new message does not push the talk button away. Both desktop and 390-pixel phone layouts were checked for notice-related movement.

## Incoming recordings

1. Turn on **Auto-record** under Incoming recordings; it starts off when the page loads.
2. Connect and start listening. Each received transmission that passes squelch is saved automatically.
3. Use **Play**, **Download**, or **Delete** next to a saved recording. Downloads are mono PCM WAV files at the actual receive rate, before the speaker-volume setting.

Recordings are stored in IndexedDB on this browser/device, and survive a normal reload. They are not on the Flipper SD card, in GitHub, or in cloud storage. Browser data clearing/private browsing/storage eviction can remove them, so download important recordings. The app requests persistent storage when auto-record is enabled, but the browser decides whether to grant it. See [MDN browser storage guidance](https://developer.mozilla.org/en-US/docs/Web/API/Storage_API/Storage_quotas_and_eviction_criteria).

Normal transmission end allows 350 ms for queued tail packets. Pause, channel/rate changes, PTT, disconnect, disabling recording, and backgrounding finish the current clip. Missing packet intervals are saved as silence and identified in the recording details; no speech is invented. Long uninterrupted transmissions become consecutive ten-minute parts to bound memory use. A storage failure keeps that clip available for download in the current session and turns auto-record off. Keep the page in front; abrupt browser/process termination can lose an unfinished clip.

## Record and repeat your message

1. Choose **Record message** and allow microphone access. This records locally, without transmitting. Choose **Finish recording** (or it finishes at 45 seconds).
2. Preview the saved message. The latest message is retained in this browser after a normal reload when storage is available.
3. Choose **Number of sends** (the first send counts), or **Running time** in seconds/minutes.
4. Set **Pause between sends** in seconds/minutes. This is the delay after a complete message, not a start-to-start interval.
5. Connect the Flipper, choose the channel and select **Start repeats**. Use **Stop** to cancel immediately.

The page waits and retries when the radio reports a busy channel. Missed/busy attempts do not count as completed sends. Timed runs do not start a message unless its estimated duration fits before the deadline. Count mode allows 1–1000 sends; time mode allows up to 60 minutes; pauses range from 1 second to 60 minutes. Existing Flipper transmit permissions and watchdogs still apply. Switching away from the page, disconnecting or an unrecoverable send error stops the repeat run; it does not resume silently later. A deadline also cancels an active timed send. Browser and Bluetooth timing remain approximate, and the firmware watchdog provides the existing fallback if contact is lost.

## Checks and limits

- All 32 offline tests passed, including full WAV duration, little-endian PCM, missing packets, channel/PTT boundaries, squelch hysteresis, long recording splits, repeat counts, timed windows, busy retries, and cancellation.
- Chrome with an isolated simulated radio: received 79 blocks (25,359 samples, 7.9246875 seconds), stored and downloaded a valid 3.2 kHz mono WAV, played it, and retained it after reload. A weaker −103 dBm simulated carrier stayed muted and created no extra recording.
- An isolated synthetic microphone exercised the real AudioWorklet capture path and stored a 24.5-second message, without accessing the system microphone.
- Three planned sends produced exactly three simulated transmissions. Stop during a send cancelled it and reported zero complete sends.
- Desktop and 390-pixel phone-width checks measured **0 pixels** of PTT movement when an alert appeared.
- No real Flipper/RF or iPhone/Bluefy run was possible during this update; the device was disconnected. Those remain the user's acceptance tests.

The local simulated radio/microphone fixtures are excluded from the published website. The site contains only the actual browser client.
