"""Generate all channel prompts from supplied WAVs or macOS speech synthesis.

Outputs stay in build/ by default. No device access or radio transmission.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, help='Directory of ch01.wav ... ch16.wav; otherwise use macOS say')
    parser.add_argument('--voice', default='Samantha (English (US))', help='Installed macOS say voice')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/generated-voice')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for channel in range(1, 17):
        stem = f'ch{channel:02}'
        if args.input_dir:
            source = args.input_dir / (stem + '.wav')
            if not source.is_file():
                raise FileNotFoundError(source)
        else:
            source = args.out / (stem + '.aiff')
            subprocess.run(['say', '-v', args.voice, '-r', '155', '-o', str(source), f'Testing channel {channel}.'], check=True)
        wav = args.out / (stem + '-input.wav')
        subprocess.run([os.environ.get('FFMPEG', 'ffmpeg'), '-v', 'error', '-y', '-i', str(source), '-ar', '48000', '-ac', '1', '-c:a', 'pcm_s16le', str(wav)], check=True)
        rate, samples = wavfile.read(wav)
        x = samples.astype(float) / 32768
        x = signal.sosfilt(signal.butter(4, [250, 2800], btype='bandpass', fs=rate, output='sos'), x)
        b, a = signal.bilinear([1 / (2 * np.pi * 700), 1], [1 / (2 * np.pi * 3000), 1], fs=rate)
        x = signal.lfilter(b, a, x)
        x *= .85 / max(np.max(np.abs(x)), 1e-9)
        x = np.concatenate([np.zeros(19200), x, np.zeros(9600)])
        x = np.pad(x, (0, (-len(x)) % 6))
        if not 4800 <= len(x) <= 48000 * 8:
            raise ValueError(f'{stem}: prompt including padding must fit the app limit of 0.1–8 seconds')
        pcm = np.round(x * 32767).astype(np.int16)
        bits = np.empty(len(pcm), np.uint8)
        error = 0
        for i, value in enumerate(pcm):
            error += int(value)
            bit = error >= 0
            bits[i] = bit
            error += -32767 if bit else 32768
        packed = np.packbits(bits, bitorder='little').tobytes()
        checksum = 2166136261
        for value in packed:
            checksum = ((checksum ^ value) * 16777619) & 0xffffffff
        data = struct.pack('<8sIIII', b'PMRPDM1\0', len(bits), rate, channel, checksum) + packed
        (args.out / (stem + '.pdm')).write_bytes(data)
        decoded = signal.sosfilt(signal.butter(6, 3000, fs=rate, output='sos'), bits.astype(float) * 2 - 1)
        wavfile.write(args.out / (stem + '-preview.wav'), rate, np.int16(np.clip(decoded * 32767, -32768, 32767)))
        manifest.append({'channel':channel, 'text':f'Testing channel {channel}.', 'file':stem + '.pdm', 'seconds':len(bits) / rate, 'sha256':hashlib.sha256(data).hexdigest()})
        print(f'{stem}: {len(bits) / rate:.2f} seconds')
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
