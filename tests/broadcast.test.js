import test from 'node:test';
import assert from 'node:assert/strict';
import {repeatMessage,abortableDelay} from '../web/broadcast.js';
function scenario(overrides={}){
  let time=0;const sent=[],waits=[],controller=new AbortController();
  return {sent,waits,controller, options:{pcm:new Int16Array(6000),signal:controller.signal,count:3,gapMs:2000,now:()=>time,
    send:async()=>{sent.push(time);time+=1300;},delay:async ms=>{waits.push(ms);time+=ms;},...overrides}};
}
test('Count includes the first send; pauses start after each complete message',async()=>{
  const f=scenario();assert.equal(await repeatMessage(f.options),3);assert.deepEqual(f.sent,[0,3300,6600]);assert.deepEqual(f.waits,[2000,2000]);
});
test('Timed run never starts a message that cannot finish within its window',async()=>{
  const f=scenario({durationMs:6000});assert.equal(await repeatMessage(f.options),2);assert.deepEqual(f.sent,[0,3300]);
});
test('Busy channel is retried without counting a skipped transmission',async()=>{
  let attempts=0;const f=scenario({count:2,send:async()=>{if(++attempts===1){const e=Error('busy');e.name='ChannelBusyError';throw e;}}});
  assert.equal(await repeatMessage(f.options),2);assert.equal(attempts,3);
});
test('Cancel while waiting prevents every later send',async()=>{
  const f=scenario();f.options.delay=async()=>f.controller.abort();assert.equal(await repeatMessage(f.options),1);assert.equal(f.sent.length,1);
});
test('Cancel during transmission is not counted as a completed message',async()=>{
  const f=scenario();f.options.send=async()=>f.controller.abort();assert.equal(await repeatMessage(f.options),0);
});
test('Disconnect errors end scheduling instead of retrying transmission',async()=>{
  const f=scenario({send:async()=>{throw Error('Disconnected');}});await assert.rejects(repeatMessage(f.options),/Disconnected/);assert.deepEqual(f.waits,[]);
});
test('Invalid plans and too-long messages are rejected before transmitting',async()=>{
  for(const bad of [{count:0},{count:1.5},{gapMs:0},{durationMs:3600001},{durationMs:1000},{pcm:new Int16Array(270001)}]){
    const f=scenario(bad);await assert.rejects(repeatMessage(f.options));assert.equal(f.sent.length,0);
  }
});
test('An aborted delay finishes promptly',async()=>{
  const c=new AbortController(),p=abortableDelay(60000,c.signal);c.abort();await assert.rejects(p,/Stopped/);
});
