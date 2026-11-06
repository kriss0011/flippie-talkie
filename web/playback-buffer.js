// A continuous sample queue. Network callbacks never stop an audible source.
export class RadioPlayback {
  constructor(inputRate, outputRate) {
    this.inputRate = inputRate;
    this.outputRate = outputRate;
    this.capacity = Math.ceil(inputRate * 4);
    this.ring = new Float32Array(this.capacity);
    this.target = inputRate * 0.24;
    this.underruns = this.overruns = this.lost = this.late = 0;
    this.reset();
  }
  reset() {
    this.read = this.write = 0;
    this.playing = this.draining = false;
    this.sequence = null;
    this.last = this.fade = 0;
    this.average = this.target;
  }
  end() {
    this.draining = true;
  }
  begin() {
    this.draining = false;
  }
  push(pcm, sequence) {
    if (this.sequence !== null) {
      const delta = (sequence - this.sequence) & 65535;
      if (delta === 0 || delta > 32768) {
        this.late++;
        return;
      }
      if (delta > 1) {
        this.lost += delta - 1;
        // Preserve short missing intervals honestly, with a fade to silence.
        // A long outage begins a fresh buffer; never replay old speech.
        if (delta > 4) this.reset();
        else {
          const previous = this.write ? this.ring[(this.write - 1) % this.capacity] : 0;
          for (let i = 0; i < (delta - 1) * pcm.length; i++)
            this.ring[this.write++ % this.capacity] =
              previous * Math.max(0, 1 - i / (this.inputRate * 0.005));
        }
      }
    }
    this.sequence = sequence;
    for (const value of pcm) this.ring[this.write++ % this.capacity] = value / 32768;
    if (this.write - this.read > this.inputRate * 0.8) {
      this.read = this.write - this.target;
      this.overruns++;
      this.fade = 0;
      this.average = this.target;
    }
  }
  render(output) {
    const queued = this.write - this.read;
    if (!this.playing && (queued >= this.target || (this.draining && queued > 0))) {
      this.playing = true;
      this.fade = 0;
    }
    // Correct clock drift gently instead of periodically deleting buffered audio.
    this.average += 0.003 * (queued - this.average);
    const correction = Math.max(
      0.99,
      Math.min(1.01, 1 + (this.average - this.target) / (this.inputRate * 8)),
    );
    const step = (this.inputRate / this.outputRate) * correction;
    for (let i = 0; i < output.length; i++) {
      if (this.playing && this.read < this.write) {
        const base = Math.floor(this.read),
          fraction = this.read - base;
        const a = this.ring[base % this.capacity];
        const b = this.ring[Math.min(base + 1, this.write - 1) % this.capacity];
        this.fade = Math.min(1, this.fade + 1 / (this.outputRate * 0.005));
        this.last = (a + (b - a) * fraction) * this.fade;
        output[i] = this.last;
        this.read = Math.min(this.write, this.read + step);
      } else {
        if (this.playing) {
          if (!this.draining) this.underruns++;
          this.playing = false;
          this.average = this.target;
        }
        this.last *= 0.98;
        output[i] = Math.abs(this.last) > 1e-6 ? this.last : 0;
      }
    }
  }
  stats() {
    return {
      bufferedMs: Math.round(((this.write - this.read) * 1000) / this.inputRate),
      underruns: this.underruns,
      overruns: this.overruns,
      lost: this.lost,
      late: this.late,
    };
  }
}
