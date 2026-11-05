"""Timestamp-aware, non-generative PMR reconstruction for the USB recorder."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np
from scipy import ndimage, signal
from scipy.io import wavfile

FS = 8000
DTYPE = np.dtype([('cycles', '<u4'), ('rssi', 'i1'), ('bit', 'u1'),
                  ('valid', 'u1'), ('tag', 'u1')])


def load_capture(path):
    path = Path(path)
    raw = path.read_bytes()
    if len(raw) < 40 or (len(raw) - 40) % 8:
        raise ValueError('Truncated capture')
    header = struct.unpack('<8s8I', raw[:40])
    if header[0] != b'PMRCAP3\0' or header[1] != 40 or header[-1] != 8 or header[6] <= 0:
        raise ValueError('Unsupported capture header')
    data = np.frombuffer(raw[40:], dtype=DTYPE)
    if len(data) < 2:
        raise ValueError('No audio samples')
    delta = np.diff(data['cycles'].astype(np.int64))
    delta[delta < 0] += 2**32
    if not np.all(delta > 0):
        raise ValueError('Non-monotonic timestamps')
    times = np.r_[0, np.cumsum(delta)] / header[6]
    meta_path = path.with_suffix('.json')
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    digest = hashlib.sha256(raw).hexdigest()
    if meta.get('sha256', digest) != digest:
        raise ValueError('Capture integrity check failed')
    return header, data, times, meta, digest


def reduce_hiss(audio, fs=FS):
    """Bounded spectral attenuation. No gain above unity; no time-varying boosting."""
    if len(audio) < 1024 or np.max(np.abs(audio)) < 1e-12:
        return audio.copy()
    _, tt, spectrum = signal.stft(audio, fs, nperseg=256, noverlap=192)
    power = abs(spectrum)**2
    # Exclude padded edges. The frequency median prevents narrow persistent
    # tones from being mistaken for broadband noise.
    inside = (tt > .032) & (tt < (len(audio) / fs - .032))
    if inside.sum() < 8:
        return audio.copy()
    floor = np.quantile(power[:, inside], .2, axis=1) / -np.log(.8)
    floor = np.minimum(floor, ndimage.median_filter(floor, size=9, mode='nearest'))
    floor = ndimage.uniform_filter1d(floor, 3, mode='nearest')
    gain = np.sqrt(np.maximum(1 - .8 * floor[:, None] / np.maximum(power, 1e-20), .35**2))
    gain = ndimage.uniform_filter(gain, size=(3, 5), mode='nearest')
    _, clean = signal.istft(spectrum * gain, fs, nperseg=256, noverlap=192)
    return clean[:len(audio)]


def decode(path, out):
    path, out = Path(path), Path(out)
    header, data, times, meta, digest = load_capture(path)
    grid = np.arange(int(np.floor(times[-1] * FS)) + 1) / FS
    reference, filtered = np.zeros(len(grid)), np.zeros(len(grid))
    cleaned, covered = np.zeros(len(grid)), np.zeros(len(grid), dtype=bool)
    tags = data['tag'] if header[5] in (100, 110, 300) else np.zeros(len(data))
    valid = data['valid'] != 0
    # Missing or invalid samples and tuning changes are not invented audio.
    boundaries = np.r_[0, np.flatnonzero((np.diff(times) > .003) |
                                        (np.diff(tags.astype(int)) != 0) |
                                        (valid[1:] != valid[:-1])) + 1, len(data)]
    band = signal.butter(4, [180, 2600], fs=FS, btype='bandpass', output='sos')
    de_b, de_a = signal.butter(1, 450, fs=FS)
    segments = []
    for lo, hi in zip(boundaries[:-1], boundaries[1:]):
        if hi - lo < 40 or not valid[lo]:
            continue
        indices = np.flatnonzero((grid >= times[lo]) & (grid <= times[hi - 1]))
        if len(indices) < 40:
            continue
        raw = np.interp(grid[indices], times[lo:hi], data['rssi'][lo:hi].astype(float) / 2 - 74)
        base = signal.sosfiltfilt(band, raw)
        soft = signal.lfilter(de_b, de_a, base)
        clean = reduce_hiss(soft)
        edge = np.ones(len(indices))
        n = min(40, len(edge) // 3)
        edge[:n], edge[-n:] = np.linspace(0, 1, n), np.linspace(1, 0, n)
        reference[indices], filtered[indices], cleaned[indices] = base*edge, soft*edge, clean*edge
        covered[indices] = True
        segments.append({'start_s': float(times[lo]), 'end_s': float(times[hi - 1]),
                         'tag': int(tags[lo]), 'median_rssi_dbm': float(np.median(raw))})
    # Only mute a release tail when there is a large, sustained measured drop.
    # A quiet syllable or a weak carrier alone is insufficient evidence.
    release = None
    rr = data['rssi'].astype(float) / 2 - 74
    if times[-1] > 1 and header[5] not in (100, 110):
        tail_floor = float(np.median(rr[(times > times[-1]-.25) & valid])) if valid[-1] else 0
        if tail_floor < -87:
            smooth_rssi = ndimage.median_filter(rr, size=101, mode='nearest')
            on = np.flatnonzero(valid & (smooth_rssi > tail_floor+8))
            if len(on):
                end = times[on[-1]]
                before = valid & (times > end-.2) & (times < end-.04)
                after = valid & (times > end+.05)
                if .3 < times[-1]-end < 3 and before.any() and after.any() and \
                        np.median(rr[before])-np.median(rr[after]) > 8:
                    release = float(end)
                    envelope = np.clip((end-grid)/.02, 0, 1)
                    reference *= envelope
                    filtered *= envelope
                    cleaned *= envelope
    out.mkdir(parents=True, exist_ok=True)
    # Filtered/cleaned share exactly the same constant gain. Suppression is not
    # hidden by boosting the cleaned file back to the original noise loudness.
    rms = np.sqrt(np.mean(filtered[covered]**2)) if covered.any() else 0
    peak = max(np.max(abs(filtered)), np.max(abs(cleaned)), 1e-12)
    common = min(.12/max(rms, 1e-12), .85/peak, 1000)
    ref_gain = min(common, .85/max(np.max(abs(reference)), 1e-12))
    audio = {}
    for name, y, gain in [('reference-full', reference, ref_gain),
                           ('filtered-full', filtered, common), ('cleaned-full', cleaned, common)]:
        pcm = np.round(y * gain * 32767).astype(np.int16)
        if not np.isfinite(y).all() or np.max(abs(y*gain)) > .851 or pcm[~covered].any():
            raise ValueError('Audio validation failed')
        wavfile.write(out / (name + '.wav'), FS, pcm)
        audio[name] = {'duration_s': len(pcm)/FS, 'gain': float(gain),
                       'rms_dbfs': float(20*np.log10(max(np.sqrt(np.mean((pcm/32768.)**2)), 1e-12)))}
    report = {'source': str(path), 'sha256': digest, 'profile': header[5], 'samples': len(data),
              'duration_s': len(grid)/FS, 'mean_sample_rate_hz': (len(data)-1)/times[-1],
              'valid_fraction': float(valid.mean()), 'missing_timeline_s': float((~covered).sum()/FS),
              'max_gap_ms': float(np.diff(times).max()*1000), 'segments': segments,
              'carrier_release_quieted_at_s': release, 'audio': audio,
              'device_diagnostics': meta.get('diagnostics'), 'receiver': meta.get('receiver'),
              'processing': 'Timestamp resampling at 8 kHz; 180-2600 Hz bandpass; optional 450 Hz single-pole treble rolloff; bounded spectral hiss reduction with 0.35 gain floor. Filtered and cleaned share one constant gain. Invalid intervals and gaps >3 ms remain silent. Short scheduling gaps are interpolated, not recovered. Full duration retained. No generated speech.',
              'limitations': 'The filter is experimental, not a measured exact inverse of the radio response. Noise reduction can remove speech detail. Lower output noise is not proof of better intelligibility or reception range.'}
    (out/'analysis.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(decode(args.capture, args.out), indent=2))
