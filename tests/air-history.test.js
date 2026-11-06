import test from 'node:test';
import assert from 'node:assert/strict';
import {AirHistory, spectrum, SPECTRUM_TOP_HZ} from '../web/air-history.js';

const tone = (hz, rate, samples, level = 0.5) =>
  Int16Array.from({length: samples}, (_, i) =>
    Math.round(Math.sin((2 * Math.PI * hz * i) / rate) * 32767 * level),
  );
const peakBin = bins => bins.indexOf(Math.max(...bins));

test('Spectrum places a tone on the shared axis at any sample rate', () => {
  for (const rate of [3200, 4000, 6000]) {
    const bins = spectrum(tone(1000, rate, 256), rate, 64),
      expected = Math.floor((1000 / SPECTRUM_TOP_HZ) * 64);
    assert.ok(Math.abs(peakBin(bins) - expected) <= 2, `${rate} Hz sample rate`);
  }
});

test('Spectrum leaves bins above Nyquist empty and silence dark', () => {
  const bins = spectrum(tone(800, 3200, 256), 3200, 64),
    nyquistBin = Math.ceil((1600 / SPECTRUM_TOP_HZ) * 64);
  assert.ok(bins.slice(nyquistBin).every(value => value === 0));
  assert.ok(spectrum(new Int16Array(256), 4000, 64).every(value => value === 0));
});

test('Columns land on the fixed time base, newest last, with gaps left empty', () => {
  const air = new AirHistory({spanMs: 1000, columnMs: 50});
  air.push('rx', tone(500, 4000, 400), 4000, 10000);
  const columns = air.visible(10000);
  assert.equal(columns.length, 20);
  assert.equal(columns.filter(Boolean).length, 2);
  assert.ok(columns.at(-1) && columns.at(-2));
  assert.ok(columns.at(-1).high > 0.4 && columns.at(-1).low < -0.4);
  assert.equal(air.visible(10500).filter(Boolean).length, 2);
  assert.equal(air.visible(11100).filter(Boolean).length, 0);
});

test('Ring slots never show stale audio from a previous lap', () => {
  const air = new AirHistory({spanMs: 1000, columnMs: 50});
  air.push('rx', tone(500, 4000, 200), 4000, 1000);
  assert.equal(air.visible(2000).filter(Boolean).length, 0);
});

test('Timeline merges one transmission and splits on a real gap or a change of direction', () => {
  const air = new AirHistory();
  air.mark('rx', 1000);
  air.mark('rx', 1800);
  air.mark('rx', 5000);
  air.mark('tx', 5100);
  assert.deepEqual(
    air.events.map(event => [event.kind, event.start, event.end]),
    [
      ['rx', 1000, 1800],
      ['rx', 5000, 5000],
      ['tx', 5100, 5100],
    ],
  );
});

test('A transmission counts as gated only if squelch held it shut throughout', () => {
  const air = new AirHistory();
  air.mark('rx', 1000, true);
  air.mark('rx', 1200, false);
  air.mark('rx', 1400, true);
  assert.equal(air.events[0].gated, false);
});

test('A live microphone monitor shows in its lane but never in the air log', () => {
  const air = new AirHistory({spanMs: 1000, columnMs: 50});
  air.push('mic', tone(500, 6000, 600), 6000, 10000, {log: false});
  assert.equal(air.events.length, 0);
  assert.equal(air.visible(10000, 'mic').filter(Boolean).length, 2);
  assert.equal(air.visible(10000, 'rx').filter(Boolean).length, 0);
  assert.ok(air.ridges(10000, 100, 3).every(row => row === null));
});

test('Pulse rows run oldest to newest and speech you send outranks reception', () => {
  const air = new AirHistory({spanMs: 2000, columnMs: 50});
  air.push('rx', tone(500, 4000, 400), 4000, 10000, {gated: true});
  air.push('rx', tone(500, 4000, 400), 4000, 10400);
  air.push('mic', tone(800, 6000, 600), 6000, 10400, {kind: 'tx'});
  const rows = air.ridges(10400, 100, 6);
  assert.equal(rows.length, 6);
  assert.equal(rows.at(-1).kind, 'tx');
  assert.ok(rows.filter(Boolean).some(row => row.kind === 'rx' && row.gated));
  assert.equal(rows[3], null);
});

test('A saved recording links to the transmission it overlaps', () => {
  const air = new AirHistory();
  air.mark('tx', 2000);
  air.mark('rx', 5000);
  air.mark('rx', 6000);
  assert.equal(air.link('clip', 5200, 0.5).kind, 'rx');
  assert.equal(air.events[1].recording, 'clip');
  assert.equal(air.link('late', 90000, 1), null);
});

test('Old timeline events are dropped after the keep window', () => {
  const air = new AirHistory({keepMs: 1000});
  air.mark('rx', 0);
  air.mark('tx', 5000);
  assert.deepEqual(
    air.events.map(event => event.kind),
    ['tx'],
  );
});
