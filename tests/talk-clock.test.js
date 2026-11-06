import test from 'node:test';
import assert from 'node:assert/strict';
import {talkClock} from '../web/tx-timing.js';

test('Talk clock counts up, then counts down to the 60-second limit', () => {
  assert.equal(talkClock(0), '0:00');
  assert.equal(talkClock(12400), '0:12');
  assert.equal(talkClock(49900), '0:49');
  assert.equal(talkClock(50000), '10 s left');
  assert.equal(talkClock(59100), '1 s left');
  assert.equal(talkClock(61000), '0 s left');
});
