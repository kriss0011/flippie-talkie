# Research

How Flippie Talkie gets analog FM voice out of the Flipper Zero's CC1101, a digital packet radio with no FM demodulator.

- **Paper:** [read on GitHub](rssi-slope-fm-on-cc1101.md) or [download the PDF](rssi-slope-fm-on-cc1101.pdf). It's an informal report and hasn't been peer reviewed.
- **Data:** [`data/`](data/) holds the tone test results behind the tables and figures. Raw captures aren't published, because some contain private test speech.

To redraw the figures and rebuild the PDF, you need Google Chrome and the Python `markdown` package:

```sh
python3 research/build.py
```

If you know of earlier work on receiving voice with a CC1101, please [open an issue](https://github.com/kriss0011/flippie-talkie/issues) so the paper can credit it.
