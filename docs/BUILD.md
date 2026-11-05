# Build and development

## Get the project

```sh
git clone https://github.com/kriss0011/flipper-pmr-radio.git
cd flipper-pmr-radio
```

This repository is private: authenticate Git using an account with access. No GitHub token belongs in project files or remote URLs. No Git LFS is required for the current files.

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

The ranges in `requirements.txt` describe supported dependencies, not an exact environment lock. The original capture data, source and validation results are retained so results can be compared when dependencies change.

## Build the two FAPs

The known-good firmware base is:

```text
https://github.com/Flipper-XFW/Xtreme-Firmware.git
6523edcc65af2fa1a72e0dc3b0bb1f9929744877
API 49.2
```

On macOS/Linux with Git, Python 3 and curl available:

```sh
bash tools/setup_sdk.sh
bash tools/build.sh
```

`setup_sdk.sh` clones the pinned firmware into `.cache/xtreme-firmware` and initializes its library/protobuf submodules. It deliberately skips the unrelated external-app catalogue. `build.sh` copies the two source directories into `applications_user/`, calls the firmware's `fbt` build tool, and copies the resulting apps and existing voice assets into `build/sd/`. On first use `fbt` downloads its versioned toolchain; this requires internet access and substantial disk space. Its “applications/external: missing manifest” warning is expected when skipping the catalogue. Both FAP build checks must still succeed.

Install the contents of `build/sd/` using the README procedure. The build command does **not** flash firmware or install anything on the connected Flipper. The supplied releases remain untouched. A native Windows firmware build was not verified; use a suitable macOS/Linux build environment or the supplied matching-API FAPs.

To reuse an existing SDK at exactly the pinned commit:

```sh
export PMR_SDK_DIR=/absolute/path/to/xtreme-firmware
bash tools/setup_sdk.sh
bash tools/build.sh
```

The scripts reject a different revision and will not overwrite differing app sources in an existing SDK. For iterative changes, edit `firmware/` in this repository, then move aside the previous `applications_user/pmr_capture` and `applications_user/pmr_connect` copies before rebuilding. This protects separate edits in another checkout. SDK/toolchain/build outputs are ignored by Git.

To build manually, copy `firmware/pmr_capture` and `firmware/pmr_connect` into the SDK's `applications_user/`, initialize the required submodules, and run these from the SDK root:

```sh
FBT_NO_SYNC=1 ./fbt build APPSRC=applications_user/pmr_capture
FBT_NO_SYNC=1 ./fbt build APPSRC=applications_user/pmr_connect
```

The outputs are `build/f7-firmware-C/.extapps/pmr_capture.fap` and `pmr_connect.fap`. Filename `pmr_capture` is historical; the app is displayed as PMR Radio.

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

## Decode an earlier raw capture

`tools/pmr_audio.py` handles the timestamped **PMRCAP3** format used by the later USB capture experiments:

```sh
python tools/pmr_audio.py \
  archive/outputs/receiver-v10/recordings/capture-20261105T153546.012816Z.pmr \
  --out local/decoded-example
```

The output includes full-duration reference, filtered and cleaned WAVs plus an analysis report. It does not invent speech. Invalid intervals and larger sample gaps remain silent; small scheduling gaps are interpolated. Earlier capture formats require their matching historical decoder; see [EXPERIMENTS.md](EXPERIMENTS.md).

## Device diagnostics

The maintained `tools/flipper_cli.py` library uses pyserial and automatically selects a single Flipper USB port. Set `FLIPPER_PORT` if several devices are attached, for example `/dev/cu.usbmodemflip_Dobby1` on the original Mac or the appropriate `/dev/ttyACM...`/`COM...` on another system. It is a library, not an installer. Close qFlipper/browser USB before using it.

The original BLE capacity, stream/watchdog, USB and screenshot scripts are preserved under `archive/work/pmr-connect/`. They retain original paths/device assumptions and are diagnostic experiments, not an automatic test suite. In particular `ble_stream_test.py` intentionally **transmits speech on channel 9** and omits STOP to test the watchdog. Read and adapt those scripts before running; use the browser for normal operation.
