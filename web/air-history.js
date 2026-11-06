// Turns live audio into a rolling record of the air, on one fixed time base.
// Holds data only; drawing lives elsewhere so this stays testable without a browser.
export const FFT_SIZE = 256;
export const SPECTRUM_TOP_HZ = 2400;
const FLOOR_DB = -72;

const hann = Float32Array.from(
  {length: FFT_SIZE},
  (_, i) => 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (FFT_SIZE - 1)),
);

// In-place radix-2 FFT.
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let size = 2; size <= n; size <<= 1) {
    const step = (-2 * Math.PI) / size;
    for (let start = 0; start < n; start += size) {
      for (let k = 0; k < size / 2; k++) {
        const wr = Math.cos(step * k),
          wi = Math.sin(step * k),
          a = start + k,
          b = a + size / 2,
          tr = re[b] * wr - im[b] * wi,
          ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}

// Magnitudes on a shared 0 to SPECTRUM_TOP_HZ axis, 0-255, so the different
// receive and microphone sample rates line up. Bins above Nyquist stay empty.
export function spectrum(samples, rate, bins) {
  const re = new Float32Array(FFT_SIZE),
    im = new Float32Array(FFT_SIZE),
    offset = Math.max(0, samples.length - FFT_SIZE),
    used = samples.length - offset;
  for (let i = 0; i < used; i++)
    re[FFT_SIZE - used + i] = (samples[offset + i] / 32768) * hann[FFT_SIZE - used + i];
  fft(re, im);
  const out = new Uint8Array(bins),
    binHz = rate / FFT_SIZE;
  for (let b = 0; b < bins; b++) {
    const from = (b / bins) * SPECTRUM_TOP_HZ,
      to = ((b + 1) / bins) * SPECTRUM_TOP_HZ;
    if (from >= rate / 2) break;
    const first = Math.floor(from / binHz),
      last = Math.min(FFT_SIZE / 2 - 1, Math.max(first, Math.ceil(to / binHz) - 1));
    let peak = 0;
    for (let k = first; k <= last; k++) peak = Math.max(peak, re[k] ** 2 + im[k] ** 2);
    const db = 10 * Math.log10(peak / (FFT_SIZE / 4) ** 2 + 1e-12);
    out[b] = Math.round(Math.max(0, Math.min(1, 1 - db / FLOOR_DB)) * 255);
  }
  return out;
}

export class AirHistory {
  constructor({spanMs = 30000, columnMs = 50, bins = 64, keepMs = 600000} = {}) {
    this.spanMs = spanMs;
    this.columnMs = columnMs;
    this.bins = bins;
    this.keepMs = keepMs;
    this.length = Math.round(spanMs / columnMs);
    this.lanes = {rx: new Array(this.length).fill(null), mic: new Array(this.length).fill(null)};
    this.events = [];
    this.updatedAt = 0;
  }

  // Slices audio into columns ending at `now`, so bursty delivery still lands in time order.
  // The mic lane carries a live monitor (kind 'mic') and speech on the air (kind 'tx').
  push(lane, pcm, rate, now, {kind = lane, gated = false, log = true} = {}) {
    const size = Math.max(1, Math.round((rate * this.columnMs) / 1000)),
      steps = Math.floor(pcm.length / size),
      lastIndex = Math.floor(now / this.columnMs),
      ring = this.lanes[lane];
    for (let s = 0; s < steps; s++) {
      const slice = pcm.subarray(s * size, (s + 1) * size),
        index = lastIndex - (steps - 1 - s);
      let low = 0,
        high = 0;
      for (const sample of slice) {
        if (sample < low) low = sample;
        if (sample > high) high = sample;
      }
      const window = pcm.subarray(Math.max(0, (s + 1) * size - FFT_SIZE), (s + 1) * size);
      ring[index % this.length] = {
        index,
        kind,
        gated,
        low: low / 32768,
        high: high / 32768,
        spectrum: spectrum(window, rate, this.bins),
      };
    }
    this.updatedAt = now;
    if (log) this.mark(kind, now, gated);
  }

  // Merges activity into events for the air timeline; a short gap stays one event.
  mark(kind, now, gated = false) {
    const last = this.events.at(-1);
    if (last && last.kind === kind && now - last.end < 1200) {
      last.end = now;
      last.gated &&= gated;
    } else {
      this.events.push({kind, start: now, end: now, gated, recording: null});
    }
    const oldest = now - this.keepMs;
    while (this.events.length && this.events[0].end < oldest) this.events.shift();
  }

  // Ties a saved recording to the transmission it came from.
  link(recording, startedAt, seconds) {
    const end = startedAt + seconds * 1000,
      event = this.events.find(
        item => item.kind === 'rx' && item.start <= end && item.end >= startedAt,
      );
    if (event) event.recording = recording;
    return event ?? null;
  }

  // Newest-last columns for the visible span, with gaps as null.
  visible(now, lane = 'rx') {
    const last = Math.floor(now / this.columnMs),
      ring = this.lanes[lane];
    return Array.from({length: this.length}, (_, i) => {
      const index = last - (this.length - 1 - i),
        column = ring[index % this.length];
      return column?.index === index ? column : null;
    });
  }

  // Oldest-first spectrum rows for the pulse field. Speech you send outranks reception,
  // and each row keeps the strongest value in its slice of time.
  ridges(now, rowMs, count) {
    const perRow = Math.max(1, Math.round(rowMs / this.columnMs)),
      lastRow = Math.floor(now / this.columnMs / perRow),
      rows = [];
    for (let r = lastRow - count + 1; r <= lastRow; r++) {
      let row = null;
      for (let index = r * perRow; index < (r + 1) * perRow; index++) {
        const slot = index % this.length,
          sent = this.lanes.mic[slot],
          heard = this.lanes.rx[slot],
          column =
            sent?.index === index && sent.kind === 'tx'
              ? sent
              : heard?.index === index
                ? heard
                : null;
        if (!column) continue;
        row ??= {kind: column.kind, gated: column.gated, spectrum: new Uint8Array(this.bins)};
        if (column.kind === 'tx') row.kind = 'tx';
        row.gated &&= column.gated;
        for (let b = 0; b < this.bins; b++)
          row.spectrum[b] = Math.max(row.spectrum[b], column.spectrum[b]);
      }
      rows.push(row);
    }
    return rows;
  }
}
