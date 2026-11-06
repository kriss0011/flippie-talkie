// Renders the web interface against a simulated Flipper, so documentation
// screenshots need no hardware and nothing is transmitted. The simulator streams
// synthetic speech as real encoded audio, so the page's whole audio path runs.
//
//   npm install --no-save puppeteer-core
//   npm run serve &
//   CHROME=/path/to/chrome node tools/web_screenshots.mjs docs/images
import {mkdirSync} from 'node:fs';
import {join} from 'node:path';
import puppeteer from 'puppeteer-core';
import {simulateFlipper} from './simulated-flipper.mjs';

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const URL_BASE = process.env.WEB_URL || 'http://127.0.0.1:8765/';
const out = process.argv[2] || 'docs/images';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function seedMessage(page) {
  await page.evaluate(async () => {
    const {RecordingStore} = await import('./recording-store.js');
    const {wavBlob} = await import('./receiver.js');
    const message = new Int16Array(6000 * 4);
    for (let i = 0; i < message.length; i++) message[i] = Math.sin(i / 6) * 3000;
    const blob = wavBlob([message], 6000);
    await new RecordingStore().save({
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
  await page.click('#mic');
  await sleep(500);
  await page.evaluate(() => {
    const toggle = document.getElementById('auto-record');
    if (!toggle.checked) toggle.click();
  });
}

const air = (page, ...args) => page.evaluate((...a) => window.flipperSim.air(...a), ...args);

async function hold(page, ms) {
  const box = await (await page.$('#ptt')).boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await sleep(ms);
}

// A short session on the air: a clear transmission, a weak one under squelch, then you.
async function session(page) {
  await air(page, true);
  await sleep(4500);
  await air(page, false);
  await sleep(1400);
  await air(page, true, -104, 0.22);
  await sleep(2200);
  await air(page, false);
  await sleep(1400);
  await hold(page, 2800);
  await page.mouse.up();
  await sleep(1300);
}

async function open(browser, width, height) {
  const context = await browser.createBrowserContext();
  const page = await context.newPage();
  await page.setViewport({width, height, deviceScaleFactor: 2});
  await page.evaluateOnNewDocument(simulateFlipper);
  await page.goto(URL_BASE);
  await seedMessage(page);
  await page.reload();
  await sleep(500);
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
const shot = (page, name, fullPage = false) =>
  page.screenshot({path: join(out, `${name}.png`), fullPage});

const desk = await open(browser, 1440, 900);
await shot(desk, 'web-connect');
await connect(desk);
await session(desk);
await air(desk, true);
await sleep(3500);
await shot(desk, 'web-connected');
await air(desk, false);
await sleep(900);
// In a full-page capture the fixed dock would land mid-page, so it joins the flow here.
await desk.click('.recording-actions .secondary');
await sleep(300);
await desk.addStyleTag({
  content:
    '.dock{position:static;grid-column:1/-1}body{padding-bottom:0}.desk{grid-template-rows:auto}',
});
await shot(desk, 'web-full', true);

const phone = await open(browser, 390, 844);
await shot(phone, 'web-phone-connect');
await connect(phone);
await session(phone);
await air(phone, true);
await sleep(3000);
await phone.evaluate(() => window.scrollTo(0, 0));
await sleep(150);
await shot(phone, 'web-phone');
await air(phone, false);
await sleep(600);
await hold(phone, 1300);
await shot(phone, 'web-phone-talking');
await phone.mouse.up();
await browser.close();
