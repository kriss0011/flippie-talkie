# Contributing

Thanks for helping. Bug reports with exact versions and steps are as valuable as code.

## Set up

```sh
git clone https://github.com/kriss0011/flippie-talkie.git
cd flippie-talkie
npm test
npm run serve
```

The tests need Node.js 20+, a C compiler, FFmpeg and Python 3. There are no npm dependencies. [BUILD.md](docs/BUILD.md) covers the firmware build, formatting and screenshots.

## Where things live

- `firmware/flippie_talkie/`: the Flipper app. `pmr_capture.c` is Listen on Flipper, `pmr_connect.c` is Browser link and `pmr_ui.h` holds the shared screen layout.
- `web/`: the browser app. It's plain ES modules with no build step.
- `tests/`: host tests that run offline and never touch a radio.
- [HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) and [PROTOCOL.md](docs/PROTOCOL.md) explain the design.

## Before you open a pull request

- Run `npm test` and add a test for changed behaviour.
- Say how you checked it, and on what hardware. Report what you actually observed, because RF claims need real measurements.
- Refresh `docs/images/` if you changed an interface.
- Keep one term per concept across the Flipper and web apps: Listen, Browser link, Channel, Squelch, Hold to talk.
- Flipper text has to fit a 128 × 64 screen. Check it on a device or in a real capture.

## Radio rules

- Keep the transmit safeguards: the busy check, the firmware frequency check, both timeouts and the 60-second limit.
- Don't add region-unlock patches or anything that bypasses firmware transmit restrictions.
- Don't test transmitting anywhere it isn't legal, and don't commit recordings of other people.

By contributing, you agree that your work is licensed under [GPL-3.0](LICENSE).
