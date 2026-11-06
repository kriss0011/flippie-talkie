// Runs in the page before app.js. Speaks the Flipper side of docs/PROTOCOL.md.
export function simulateFlipper() {
  const RATE = 4000,
    SAMPLES = 321;
  const sim = (window.flipperSim = {
    channel: 9,
    state: 1,
    listening: 1,
    rssi: -96,
    epoch: 0,
    error: 0,
    airing: false,
    level: 0.5,
    sequence: 0,
    phase: 0,
    clock: 0,
    encoder: {index: 0},
  });
  sim.air = (on, rssi = -71, level = 0.5) => {
    sim.airing = on;
    sim.level = level;
    sim.rssi = on ? rssi : -96;
    sim.state = on ? 2 : 1;
  };
  const packet = (type, body) => {
    const frame = new Uint8Array(8 + body.length),
      view = new DataView(frame.buffer);
    frame.set([80, 82, type, sim.epoch]);
    view.setUint16(4, body.length, true);
    view.setUint16(6, sim.sequence++ & 65535, true);
    frame.set(body, 8);
    return frame;
  };
  const statusFrame = () => {
    const body = new Uint8Array(26),
      view = new DataView(body.buffer);
    body.set([sim.channel, 1, sim.state, sim.listening]);
    view.setInt8(4, sim.rssi);
    body[5] = sim.epoch;
    body[6] = sim.error;
    body[7] = 3;
    view.setUint16(10, 6000, true);
    view.setUint16(24, RATE, true);
    return packet(0x81, body);
  };
  // Harmonics under moving formants with a syllable rhythm, over radio noise.
  const speech = () => {
    const pcm = new Int16Array(SAMPLES);
    for (let i = 0; i < SAMPLES; i++) {
      const t = sim.clock++ / RATE,
        syllable = Math.max(0, Math.sin(t * 2 * Math.PI * 3.1)) ** 0.6,
        pitch = 118 + 22 * Math.sin(t * 2.3),
        first = 480 + 240 * Math.sin(t * 5.1),
        second = 1300 + 420 * Math.sin(t * 3.7 + 1);
      sim.phase += pitch / RATE;
      let voice = 0;
      for (let h = 1; h <= 16; h++) {
        const hz = h * pitch,
          shape =
            Math.exp(-(((hz - first) / 170) ** 2)) + 0.6 * Math.exp(-(((hz - second) / 240) ** 2));
        voice += Math.sin(2 * Math.PI * h * sim.phase) * shape;
      }
      const noise = (Math.random() - 0.5) * 0.07;
      pcm[i] = Math.max(-1, Math.min(1, voice * 0.3 * syllable * sim.level + noise)) * 32767;
    }
    return pcm;
  };
  const handle = bytes => {
    if (bytes[0] !== 80 || bytes[1] !== 82) return;
    const type = bytes[2],
      value = bytes[8];
    sim.epoch = type === 4 ? bytes[3] : sim.epoch;
    if (type === 2) sim.listening = value;
    if (type === 3) sim.channel = value;
    if (type === 4) {
      sim.state = 3;
      setTimeout(() => (sim.state = 4), 200);
    }
    if (type === 6) sim.state = 1;
  };
  const port = {
    async open() {},
    async close() {},
    readable: new ReadableStream({
      async start(controller) {
        const {encode} = await import('./codec.js');
        const timers = [];
        const every = (ms, work) =>
          timers.push(
            setInterval(() => {
              try {
                work();
              } catch {
                timers.forEach(clearInterval); // The page closed the port.
              }
            }, ms),
          );
        every(150, () => controller.enqueue(statusFrame()));
        every((SAMPLES / RATE) * 1000, () => {
          if (sim.airing && sim.state === 2)
            controller.enqueue(packet(0x82, encode(speech(), sim.encoder)));
        });
      },
    }),
    writable: new WritableStream({write: handle}),
  };
  Object.defineProperty(navigator, 'serial', {value: {requestPort: async () => port}});
  Object.defineProperty(navigator, 'bluetooth', {value: {}});
}
