// Renders the web interface against a simulated Flipper, so documentation
// screenshots need no hardware and nothing is transmitted.
//
//   npm install --no-save puppeteer-core
//   npm run serve &
//   CHROME=/path/to/chrome node tools/web_screenshots.mjs docs/images
import {mkdirSync} from 'node:fs';
import {join} from 'node:path';
import puppeteer from 'puppeteer-core';

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const URL_BASE = process.env.WEB_URL || 'http://127.0.0.1:8765/';
const out = process.argv[2] || 'docs/images';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

// Runs in the page before app.js. Speaks the status half of docs/PROTOCOL.md.
function simulateFlipper() {
  const sim = (window.flipperSim = {
    channel: 9,
    state: 1,
    listening: 1,
    rssi: -96,
    epoch: 0,
    error: 0,
  });
  const statusFrame = () => {
    const body = new Uint8Array(26),
      view = new DataView(body.buffer);
    body.set([sim.channel, 1, sim.state, sim.listening]);
    view.setInt8(4, sim.rssi);
    body[5] = sim.epoch;
    body[6] = sim.error;
    body[7] = 3;
    view.setUint16(10, 6000, true);
    view.setUint16(24, 4000, true);
    const frame = new Uint8Array(8 + body.length),
      header = new DataView(frame.buffer);
    frame.set([80, 82, 0x81, sim.epoch]);
    header.setUint16(4, body.length, true);
    frame.set(body, 8);
    return frame;
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
      start(controller) {
        const timer = setInterval(() => {
          try {
            controller.enqueue(statusFrame());
          } catch {
            clearInterval(timer); // The page closed the port.
          }
        }, 150);
      },
    }),
    writable: new WritableStream({write: handle}),
  };
  Object.defineProperty(navigator, 'serial', {value: {requestPort: async () => port}});
  Object.defineProperty(navigator, 'bluetooth', {value: {}});
}

async function seedRecordings(page) {
  await page.evaluate(async () => {
    const {RecordingStore} = await import('./recording-store.js');
    const {wavBlob} = await import('./receiver.js');
    const store = new RecordingStore();
    const tone = seconds => {
      const pcm = new Int16Array(4000 * seconds);
      for (let i = 0; i < pcm.length; i++) pcm[i] = Math.sin(i / 8) * 4000;
      return wavBlob([pcm], 4000);
    };
    const clips = [
      [9, 14, 0],
      [9, 6, 240],
      [3, 31, 1800],
    ];
    for (const [channel, duration, age] of clips) {
      const blob = tone(duration);
      await store.save({
        id: `demo-${channel}-${duration}`,
        kind: 'receive',
        channel,
        duration,
        rate: 4000,
        startedAt: new Date('2026-11-06T10:30:00').getTime() - age * 1000,
        bytes: blob.size,
        blob,
        reason: 'Transmission ended',
        missingSamples: 0,
      });
    }
    const message = new Int16Array(6000 * 4);
    for (let i = 0; i < message.length; i++) message[i] = Math.sin(i / 6) * 3000;
    const blob = wavBlob([message], 6000);
    await store.save({
      id: 'outgoing-message',
      kind: 'message',
      rate: 6000,
      startedAt: Date.now(),
      bytes: blob.size,
      blob,
    });
  });
}

async function connect(page) {
  await page.select('#transport', 'usb');
  await page.click('#connect');
  await page.waitForFunction(() => document.body.classList.contains('connected'));
  await sleep(300);
}

async function open(browser, width, height, seed = true) {
  const context = await browser.createBrowserContext();
  const page = await context.newPage();
  await page.setViewport({width, height, deviceScaleFactor: 2});
  await page.evaluateOnNewDocument(simulateFlipper);
  await page.goto(URL_BASE);
  if (seed) {
    await seedRecordings(page);
    await page.reload();
  }
  await sleep(400);
  return page;
}

mkdirSync(out, {recursive: true});
const browser = await puppeteer.launch({
  executablePath: CHROME,
  headless: 'new',
  args: [
    '--use-fake-ui-for-media-stream',
    '--use-fake-device-for-media-stream',
    '--autoplay-policy=no-user-gesture-required',
  ],
});
const shot = (page, name, fullPage = true) =>
  page.screenshot({path: join(out, `${name}.png`), fullPage});

let page = await open(browser, 1100, 900, false);
await shot(page, 'web-connect');

page = await open(browser, 1100, 900);
await connect(page);
await page.click('#mic');
await sleep(500);
await page.evaluate(() => {
  window.flipperSim.rssi = -71;
  window.flipperSim.state = 2;
});
await sleep(500);
await shot(page, 'web-connected');

page = await open(browser, 390, 844);
await connect(page);
await page.click('#mic');
await sleep(500);
await shot(page, 'web-phone', false);
const button = await page.$('#ptt');
const box = await button.boundingBox();
await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
await page.mouse.down();
await sleep(900);
await shot(page, 'web-phone-talking', false);
await page.mouse.up();
await browser.close();
