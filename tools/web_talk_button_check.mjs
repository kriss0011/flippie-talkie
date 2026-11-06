// Proves the talk button never moves: its position and size must be identical in every
// state, at every screen size. Uses a simulated Flipper, so nothing is transmitted.
//
//   npm install --no-save puppeteer-core
//   npm run serve &
//   node tools/web_talk_button_check.mjs
import puppeteer from 'puppeteer-core';
import {simulateFlipper} from './simulated-flipper.mjs';

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const URL_BASE = process.env.WEB_URL || 'http://127.0.0.1:8765/';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const b = await puppeteer.launch({
  executablePath: CHROME,
  headless: 'new',
  args: [
    '--use-fake-ui-for-media-stream',
    '--use-fake-device-for-media-stream',
    '--autoplay-policy=no-user-gesture-required',
  ],
});
let bad = 0;
for (const [w, h] of [
  [320, 640],
  [390, 844],
  [768, 1024],
  [1024, 700],
  [1280, 720],
  [1440, 900],
  [1920, 1080],
]) {
  const c = await b.createBrowserContext();
  const p = await c.newPage();
  await p.setViewport({width: w, height: h});
  const errs = [];
  p.on('pageerror', e => errs.push(e.message));
  p.on('console', m => m.type() === 'error' && errs.push(m.text().slice(0, 120)));
  await p.evaluateOnNewDocument(simulateFlipper);
  await p.goto(URL_BASE);
  await sleep(300);
  const seen = new Map();
  const rect = async name => {
    const r = await p.$eval('#ptt', e => {
      const r = e.getBoundingClientRect();
      return [r.left, r.top, r.width, r.height].map(n => Math.round(n * 10) / 10).join(',');
    });
    seen.set(name, r);
  };
  await rect('disconnected');
  await p.select('#transport', 'usb');
  await rect('usb chosen');
  await p.evaluate(() => (document.querySelector('details.help').open = true));
  await rect('help open');
  await p.evaluate(() => window.scrollTo(0, 99999));
  await rect('scrolled to end');
  await p.evaluate(() => {
    window.scrollTo(0, 0);
    document.querySelector('details.help').open = false;
  });
  await p.click('#connect');
  await p.waitForFunction(() => document.body.classList.contains('connected'));
  await sleep(200);
  await rect('connected');
  const label0 = await p.$eval('#ptt-label', e => e.textContent);
  await p.$eval('#ptt', e => e.scrollIntoView());
  const pb = await (await p.$('#ptt')).boundingBox();
  await p.mouse.click(pb.x + pb.width / 2, pb.y + pb.height / 2);
  await sleep(700);
  await rect('mic enabled by tap');
  const after = await p.evaluate(() => ({
    label: document.getElementById('ptt-label').textContent,
    state: window.flipperSim.state,
  }));
  await p.evaluate(() => {
    document.getElementById('notice').textContent =
      'A long notice that wraps on to several lines to prove that nothing beneath it moves when it appears on the page.';
    document.getElementById('notice').hidden = false;
  });
  await rect('notice');
  await p.evaluate(() => (document.getElementById('notice').hidden = true));
  await p.evaluate(() => {
    window.flipperSim.air(true);
  });
  await sleep(900);
  await rect('receiving');
  await p.evaluate(() => window.flipperSim.air(false));
  await sleep(300);
  await p.evaluate(() => window.scrollTo(0, 99999));
  await sleep(100);
  await rect('scrolled while connected');
  const bb = await (await p.$('#ptt')).boundingBox();
  await p.mouse.move(bb.x + bb.width / 2, bb.y + bb.height / 2);
  await p.mouse.down();
  await sleep(90);
  await rect('pressed');
  await sleep(380);
  await rect('opening');
  await sleep(650);
  await rect('talking');
  const talk = await p.evaluate(() => ({
    l: document.getElementById('ptt-label').textContent,
    d: document.getElementById('ptt-detail').textContent,
    b: getComputedStyle(document.getElementById('ptt-budget')).transform.slice(0, 24),
  }));
  await p.mouse.up();
  await sleep(300);
  await rect('released');
  await p.click('#disconnect');
  await sleep(300);
  await rect('disconnected again');
  const fit = await p.evaluate(() => [
    document.documentElement.scrollWidth,
    document.documentElement.clientWidth,
  ]);
  const u = new Set(seen.values());
  if (u.size !== 1) bad++;
  console.log(
    `${w}x${h}`,
    u.size === 1 ? 'STABLE' : 'MOVED',
    [...u].join(' | '),
    `states=${seen.size}`,
    label0,
    '->',
    JSON.stringify(after),
    JSON.stringify(talk),
    'fit',
    fit.join('/'),
    errs.slice(0, 2),
  );
  if (u.size !== 1) console.log([...seen]);
  await c.close();
}
await b.close();
console.log(bad ? 'FAIL' : 'PASS: talk button identical in every state at every size');
process.exitCode = bad ? 1 : 0;
