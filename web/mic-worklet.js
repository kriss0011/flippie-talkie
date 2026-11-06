// A continuous fractional sample clock avoids block-boundary drift at 44.1 or 48 kHz.
class RadioMic extends AudioWorkletProcessor {
  constructor() {
    super();
    this.enabled = false;
    this.phase = 0;
    this.previous = 0;
    this.count = 0;
    this.block = new Int16Array(321);
    this.port.onmessage = e => {
      this.enabled = e.data === true;
      this.phase = 0;
      this.count = 0;
    };
  }
  process(inputs) {
    const a = inputs[0]?.[0];
    if (!a || !this.enabled) return true;
    for (const x of a) {
      this.phase += 6000;
      if (this.phase >= sampleRate) {
        this.phase -= sampleRate;
        const y = x + ((this.previous - x) * this.phase) / 6000;
        this.block[this.count++] = Math.max(-32768, Math.min(32767, Math.round(y * 32767)));
        if (this.count === 321) {
          this.port.postMessage(this.block, [this.block.buffer]);
          this.block = new Int16Array(321);
          this.count = 0;
        }
      }
      this.previous = x;
    }
    return true;
  }
}
registerProcessor('radio-mic', RadioMic);
