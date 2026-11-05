import {RadioPlayback} from './playback-buffer.js';
class RadioSpeaker extends AudioWorkletProcessor {
  constructor() {
    super();
    this.player = new RadioPlayback(4000, sampleRate);
    this.frames = 0;
    this.port.onmessage = ({data}) => {
      if (data.type === 'rate' && data.rate !== this.player.inputRate)
        this.player = new RadioPlayback(data.rate, sampleRate);
      else if (data.type === 'reset') this.player.reset();
      else if (data.type === 'begin') this.player.begin();
      else if (data.type === 'end') this.player.end();
      else if (data.type === 'audio') this.player.push(data.pcm, data.sequence);
    };
  }
  process(inputs, outputs) {
    const output = outputs[0]?.[0];
    if (!output) return true;
    this.player.render(output);
    this.frames += output.length;
    if (this.frames >= sampleRate) {
      this.frames -= sampleRate;
      this.port.postMessage(this.player.stats());
    }
    return true;
  }
}
registerProcessor('radio-speaker', RadioSpeaker);
