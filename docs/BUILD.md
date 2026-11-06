# Build and development

## Get the project

```sh
git clone https://github.com/kriss0011/flippie-talkie.git
cd flippie-talkie
```

No Git LFS is required.

## Offline checks

Install Node.js 20+, Python 3, a C compiler and FFmpeg. On macOS, the compiler comes with Xcode Command Line Tools (`xcode-select --install`); with Homebrew already installed, `brew install node ffmpeg` supplies the other host-test programs. On Debian/Ubuntu, the relevant packages are `build-essential`, `ffmpeg`, `python3`, and a Node.js version meeting the requirement.

```sh
npm test
python3 tools/verify_bundle.py
```

No npm dependencies are required. Set `CC=/path/to/cc` or `FFMPEG=/path/to/ffmpeg` when executables are not on PATH. Each variable is an executable path, not a command plus arguments. Tests create temporary files and never connect to or transmit from Flipper.

For optional Python DSP, voice generation, and historical diagnostics:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

The ranges in `requirements.txt` describe supported dependencies, not an exact environment lock.

## Build the unified FAP

The known-good firmware base is:

```text
https://github.com/RogueMaster/flipperzero-firmware-wPlugins.git
e1837ff6a1a7d5c91a0782343a98e2e600d62da7
API 88.23
```

On macOS/Linux with Git, Python 3 and curl available:

```sh
bash tools/setup_sdk.sh
bash tools/build.sh
```

`setup_sdk.sh` clones the pinned firmware into `.cache/roguemaster-firmware` and initializes its library/protobuf submodules. It deliberately skips the unrelated external-app catalogue. `build.sh` copies the unified source directory into `applications_user/`, calls the firmware's `fbt` build tool with `tools/fbt_roguemaster.py` (selecting this app explicitly without unrelated bundled applications), and copies the resulting app and existing voice assets into `build/sd/`. On first use `fbt` downloads its versioned toolchain; this requires internet access and substantial disk space. Its “applications/external: missing manifest” warning is expected when skipping the catalogue. The FAP build check must still succeed.

Install the contents of `build/sd/` using the README procedure. The build command does **not** flash firmware or install anything on the connected Flipper. The supplied releases remain untouched. A native Windows firmware build was not verified; use a suitable macOS/Linux build environment or the supplied matching-API FAPs.

To reuse an existing SDK at exactly the pinned commit:

```sh
export PMR_SDK_DIR=/absolute/path/to/roguemaster-firmware
bash tools/setup_sdk.sh
bash tools/build.sh
```

The scripts reject a different revision and will not overwrite differing app sources in an existing SDK. For iterative changes, edit `firmware/` in this repository, then move aside the previous `applications_user/flippie_talkie` copy before rebuilding. This protects separate edits in another checkout. SDK/toolchain/build outputs are ignored by Git.

To build manually, copy `firmware/flippie_talkie` into the SDK's `applications_user/`, initialize the required submodules, and run these from the SDK root:

```sh
FBT_NO_SYNC=1 ./fbt --options=/absolute/path/to/flippie-talkie/tools/fbt_roguemaster.py build APPSRC=applications_user/flippie_talkie
```

The output is `build/f7-firmware-C/.extapps/flippie_talkie.fap`, packaged as `Flippie_Talkie.fap`. The two radio engines are internal modules of this single app.

## Generate spoken channel prompts

The existing tested PDM assets are already in `sd/apps_data/pmr_radio/voice/`. You do not need to regenerate them to install.

On macOS, list installed speech voices with `say -v '?'`, then run:

```sh
python tools/generate_voice.py --voice 'Samantha (English (US))'
```

Or provide your own mono/stereo WAV recordings on any supported host, named `ch01.wav` through `ch16.wav`:

```sh
python tools/generate_voice.py --input-dir /path/to/channel-prompts
```

FFmpeg converts the inputs to 48 kHz mono. The generator applies the existing filtering/pre-emphasis and one-bit PDM encoding, then writes `.pdm`, WAV previews and a manifest into `build/generated-voice/`. Listen to the previews, copy the generated `.pdm` files into `sd/apps_data/pmr_radio/voice/`, and run `npm test` to validate their headers, checksums and finite timing before installation. Long prompts use more Flipper memory; keep prompts short, comparable to the supplied few-second assets. Changing assets intentionally invalidates the old release checksum inventory; retain the original releases and publish a new manifest/version for changed assets.

## Decode a raw capture

`tools/pmr_audio.py` handles the timestamped **PMRCAP3** format from the USB capture experiments. Keep the matching `.json` beside the `.pmr` file:

```sh
python tools/pmr_audio.py path/to/capture.pmr --out local/decoded-example
```

The output includes full-duration reference, filtered and cleaned WAVs plus an analysis report. It does not invent speech: invalid intervals and larger sample gaps stay silent, and small scheduling gaps are interpolated.

## Screenshots

`docs/images/` is generated, so refresh it whenever the interface changes.

**Flipper screens** come from a connected device over USB. The tool uploads the built app as a temporary copy, walks the screens and removes it. It sends short key presses only, starts no test and transmits nothing:

```sh
bash tools/build.sh
python tools/flipper_screenshots.py docs/images
```

**Web screens** run against a simulated Flipper in headless Chrome, so they need no hardware:

```sh
npm install --no-save puppeteer-core
npm run serve &
node tools/web_screenshots.mjs docs/images
```

Set `CHROME` if Chrome isn't in the default macOS location.

## Formatting

C follows the firmware's `firmware/.clang-format`. JavaScript and CSS follow `.prettierrc.json`:

```sh
clang-format -i firmware/flippie_talkie/*.c firmware/flippie_talkie/*.h
npx prettier --write 'web/*.js' web/style.css
```

## Device diagnostics

`tools/flipper_cli.py` is a pyserial library that selects a single Flipper USB port automatically. Set `FLIPPER_PORT` if several devices are attached, for example `/dev/cu.usbmodemflip_<name>1` on macOS, `/dev/ttyACM0` on Linux or `COM3` on Windows. Close qFlipper and any browser USB session first.

## Cut a release

1. Update the version in `package.json`, `firmware/flippie_talkie/application.fam`, `pmr_ui.h`, `web/index.html` and `tools/package_release.py`. `npm test` fails if they disagree.
2. Add an entry to `CHANGELOG.md`.
3. Build, copy the FAP to `sd/apps/Sub-GHz/`, then run `python3 tools/package_release.py` and `python3 tools/verify_bundle.py`.
