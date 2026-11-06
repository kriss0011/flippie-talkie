// Browser-side receive gate. Firmware and radio tuning are unchanged.
export const DEFAULT_SQUELCH = -95;
export class Squelch {
  constructor(threshold = DEFAULT_SQUELCH, holdMs = 450) {
    this.threshold = threshold;
    this.holdMs = holdMs;
    this.reset();
  }
  reset() {
    this.open = false;
    this.belowSince = null;
  }
  update(receiving, rssi, now) {
    if (!receiving) {
      this.reset();
      return false;
    }
    if (this.threshold === null) {
      this.open = true;
      return true;
    }
    const enough =
      Number.isFinite(rssi) && rssi > -127 && rssi >= this.threshold - (this.open ? 4 : 0);
    if (enough) {
      this.open = true;
      this.belowSince = null;
    } else if (this.open) {
      this.belowSince ??= now;
      if (now - this.belowSince >= this.holdMs) this.reset();
    }
    return this.open;
  }
}

export function wavBlob(chunks, rate) {
  const samples = chunks.reduce((n, chunk) => n + chunk.length, 0);
  const header = new ArrayBuffer(44),
    v = new DataView(header);
  const text = (offset, s) => [...s].forEach((c, i) => v.setUint8(offset + i, c.charCodeAt(0)));
  text(0, 'RIFF');
  v.setUint32(4, 36 + samples * 2, true);
  text(8, 'WAVE');
  text(12, 'fmt ');
  v.setUint32(16, 16, true);
  v.setUint16(20, 1, true);
  v.setUint16(22, 1, true);
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * 2, true);
  v.setUint16(32, 2, true);
  v.setUint16(34, 16, true);
  text(36, 'data');
  v.setUint32(40, samples * 2, true);
  // Explicit little-endian PCM; independent of the host's typed-array endianness.
  const pcm = new ArrayBuffer(samples * 2),
    data = new DataView(pcm);
  let offset = 0;
  for (const chunk of chunks)
    for (const sample of chunk) {
      data.setInt16(offset, sample, true);
      offset += 2;
    }
  return new Blob([header, pcm], {type: 'audio/wav'});
}

export class ReceiveSession {
  constructor({
    onAudio = () => {},
    onReset = () => {},
    onEnd = () => {},
    onRecording = () => {},
    now = () => performance.now(),
    wallClock = () => Date.now(),
    maxSeconds = 600,
  } = {}) {
    Object.assign(this, {onAudio, onReset, onEnd, onRecording, now, wallClock, maxSeconds});
    this.gate = new Squelch();
    this.enabled = false;
    this.status = null;
    this.receiving = false;
    this.audible = false;
    this.endingAt = null;
    this.lastAudio = null;
    this.sequence = null;
    this.clip = null;
  }
  setThreshold(threshold) {
    if (threshold === this.gate.threshold) return;
    this.gate.threshold = threshold;
    this.gate.reset();
    this.tick();
  }
  setRecording(enabled) {
    this.enabled = enabled;
    if (!enabled) this.finish('Recording switched off');
  }
  update(status) {
    const previous = this.status;
    if (previous && (previous.channel !== status.channel || previous.rxRate !== status.rxRate))
      this.reset('Channel changed');
    this.status = status;
    if (!status.listening || status.state === 0 || status.state >= 3) {
      this.reset('Listening stopped');
      return;
    }
    if (status.state === 2) {
      this.receiving = true;
      this.endingAt = null;
    } else if (this.receiving && this.endingAt === null) this.endingAt = this.now() + 350;
    this.tick();
  }
  tick() {
    const now = this.now();
    if (this.endingAt !== null && now >= this.endingAt) {
      this.finish('Transmission ended');
      this.receiving = false;
      this.endingAt = null;
      this.sequence = null;
      this.lastAudio = null;
      this.gate.reset();
      this.audible = false;
      this.onEnd();
    }
    if (this.clip && this.lastAudio !== null && now - this.lastAudio > 1800) {
      this.finish('Audio interrupted');
      this.sequence = null;
    }
    const open = this.gate.update(this.receiving, this.status?.rssi, now);
    if (this.audible && !open) {
      this.finish('Squelch closed');
      this.sequence = null;
      this.onReset();
    }
    this.audible = open;
    return open;
  }
  push(pcm, sequence) {
    if (!this.tick()) return false;
    let missing = 0;
    if (this.sequence !== null) {
      const delta = (sequence - this.sequence) & 65535;
      if (!delta || delta > 32768) return false;
      if (delta > 1000) {
        this.finish('Audio interrupted');
        this.onReset();
      } else missing = delta - 1;
    }
    this.sequence = sequence;
    this.lastAudio = this.now();
    if (this.enabled) {
      const rate = this.status.rxRate;
      if (!this.clip) missing = 0;
      this.clip ??= {
        channel: this.status.channel,
        rate,
        startedAt: this.wallClock(),
        chunks: [],
        samples: 0,
        missingSamples: 0,
      };
      const clip = this.clip;
      if (missing) {
        const silence = new Int16Array(missing * pcm.length);
        clip.chunks.push(silence);
        clip.samples += silence.length;
        clip.missingSamples += silence.length;
      }
      // Copy before the original buffer is transferred to the speaker worklet.
      clip.chunks.push(pcm.slice());
      clip.samples += pcm.length;
      if (clip.samples >= rate * this.maxSeconds) this.finish('Continued in next part');
    }
    this.onAudio(pcm, sequence);
    return true;
  }
  finish(reason) {
    const clip = this.clip;
    this.clip = null;
    if (!clip?.samples) return;
    this.onRecording({
      channel: clip.channel,
      rate: clip.rate,
      startedAt: clip.startedAt,
      duration: clip.samples / clip.rate,
      missingSamples: clip.missingSamples,
      reason,
      blob: wavBlob(clip.chunks, clip.rate),
    });
  }
  reset(reason = 'Reception stopped') {
    this.finish(reason);
    this.gate.reset();
    this.receiving = this.audible = false;
    this.endingAt = this.lastAudio = this.sequence = null;
    this.onReset();
  }
}
