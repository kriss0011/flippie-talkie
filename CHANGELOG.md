# Changelog

## 0.10 – 2026-11-06

**Flipper app**
- New Speak text mode: type a message and the Flipper speaks it on the channel, with no phone or computer. Set repeats, pause, speed and pitch, or hear it on the Flipper first. The English voice is synthesized on the device and is robotic.
- Each send checks that the channel is clear and uses the same lead-in and 60-second limit as push-to-talk.
- Speech is a plugin inside the same FAP, loaded only while Speak text is open, so Listen keeps the memory it needs to record.

The web app is unchanged apart from its version number. See [Speak text](docs/SPEAK-TEXT.md).

## 0.9 – 2026-11-06

**Flipper app**
- Clearer screen titles and terms, matching the web app: Listen, Browser link, Transmit, Not linked, Opening.
- The signal meter no longer overlaps other text, and the channel box no longer touches the buttons.
- Menus use the full width when there's no scrollbar, and Bluetooth is spelled out everywhere.
- The Tests menu shows which channel it will transmit on, and Home shows the version.

**Web app**
- Rebuilt layout: a connection guide with numbered steps for each transport, then a page that's all radio once connected.
- Notices float above the page, so they no longer reserve empty space or move controls.
- Audio players and repeat settings appear only when there's something to play or send.
- Rewritten labels, help and error messages that say how to recover.
- The Bluetooth device filter now includes transparent Flippers and no longer lists a personal device name.

**Project**
- Prepared for open source: new README, guides, contribution and security policies, CI, and formatted source.
- Private recordings and early experiment files were removed from the tree.

No change to the radio, audio or wire protocol. See the [validation note](validation/ui-0.9/README.md).

## 0.8 – 2026-11-06

- Added a 500 ms silent carrier before speech, and finished queued speech after release. See [notes](docs/history/TX-LEAD-IN-0.8.md).

## 0.7 – 2026-11-06

- Corrected voice transmit timing on API 88. See [notes](docs/history/TX-TIMING-0.7.md).
- Web: per-channel squelch, browser recordings and repeated messages. See [notes](docs/history/WEB-0.7.md).

## 0.6 – 2026-11-05

- Ported to RogueMaster API 88.23. See [notes](docs/history/ROGUEMASTER-0.6.md).

## 0.5 – 2026-11-05

- Merged the two apps into one, with a new native interface. See [notes](docs/history/REDESIGN-0.5.md).

## 0.4 – 2026-11-05

- Renamed the project to Flippie Talkie. See [notes](docs/history/RENAME-0.4.md).

## 0.3 and earlier

- Fixed receive cutouts with buffered playback. See [notes](docs/history/SMOOTHNESS-0.3.md) and the [experiment history](docs/history/EXPERIMENTS.md).
