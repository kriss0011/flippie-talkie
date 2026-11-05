import test from 'node:test';
import assert from 'node:assert/strict';
import {Squelch, ReceiveSession, wavBlob} from '../web/receiver.js';
const status = (state = 2, rssi = -70, channel = 9, rxRate = 3200) => ({state, rssi, channel, rxRate, listening: state !== 0});
function fixture(options = {}) {
  let time = 0; const saved = [], audio = [], resets = [], ends = [];
  const session = new ReceiveSession({now: () => time, wallClock: () => 1000 + time,
    onAudio: (pcm, sequence) => audio.push({pcm, sequence}), onRecording: clip => saved.push(clip),
    onReset: () => resets.push(time), onEnd: () => ends.push(time), ...options});
  session.setRecording(true);
  return {session, saved, audio, resets, ends, at: value => {time = value;}};
}
test('Squelch blocks weak static and uses hysteresis plus hold time', () => {
  const gate = new Squelch(-90);
  assert.equal(gate.update(true, -100, 0), false);
  assert.equal(gate.update(true, -89, 100), true);
  assert.equal(gate.update(true, -93, 200), true);
  assert.equal(gate.update(true, -98, 300), true);
  assert.equal(gate.update(true, -98, 749), true);
  assert.equal(gate.update(true, -98, 750), false);
  assert.equal(gate.update(true, -93, 800), false);
  gate.threshold = null; assert.equal(gate.update(true, -110, 900), true);
  assert.equal(gate.update(false, -50, 1000), false);
});
test('Muted signals are neither played nor recorded; manually lowering squelch opens reception', () => {
  const f = fixture(); f.session.update(status(2, -100));
  assert.equal(f.session.push(new Int16Array(321), 0), false);
  assert.equal(f.session.clip, null); assert.equal(f.audio.length, 0);
  f.session.setThreshold(-105); assert.equal(f.session.push(new Int16Array(321), 1), true);
  f.session.reset(); assert.equal(f.saved.length, 1);
});
test('Incoming WAV keeps the full transmission, tail, original samples and rate', async () => {
  const f = fixture(); f.session.update(status());
  for(let i=0;i<60;i++){f.at(i*100);f.session.push(new Int16Array(321).fill(i),i);}
  f.session.update(status(1)); f.at(6100); f.session.push(new Int16Array(321).fill(1234),60);
  assert.equal(f.saved.length,0); f.at(6300);f.session.tick();
  assert.equal(f.saved.length,1);const clip=f.saved[0],v=new DataView(await clip.blob.arrayBuffer());
  assert.equal(v.getUint32(24,true),3200);assert.equal(v.getUint32(40,true),61*321*2);
  assert.equal(v.getInt16(v.byteLength-2,true),1234);assert.equal(clip.duration,61*321/3200);
  assert.equal(f.ends.length,1);
});
test('Missing blocks become silence in recordings; duplicate and old packets are not repeated', async () => {
  const f=fixture();f.session.update(status());const pcm=new Int16Array(321).fill(1000);
  f.session.push(pcm,65534);f.session.push(pcm,0);assert.equal(f.session.push(pcm,0),false);assert.equal(f.session.push(pcm,65535),false);
  f.session.reset();const c=f.saved[0],v=new DataView(await c.blob.arrayBuffer());
  assert.equal(c.missingSamples,321);assert.equal(v.getInt16(44+321*2,true),0);assert.equal(v.byteLength,44+963*2);
});
test('Channel changes, PTT, pause, disconnect and rate changes finalize separate recordings', () => {
  const f=fixture(),pcm=new Int16Array(321);
  f.session.update(status());f.session.push(pcm,1);f.session.update(status(2,-70,10));f.session.push(pcm,2);
  f.session.update(status(3,-70,10));assert.equal(f.saved.length,2);assert.deepEqual(f.saved.map(c=>c.channel),[9,10]);
  f.session.update(status(2,-70,10,4000));f.session.push(pcm,3);f.session.update(status(0));
  f.session.update(status());f.session.push(pcm,4);f.session.reset('Disconnected');
  assert.equal(f.saved.length,4);assert.equal(f.saved[2].rate,4000);assert.equal(f.saved[3].reason,'Disconnected');
});
test('Auto-record off saves nothing; stop saves the active clip; long receives split without dropping samples', () => {
  const f=fixture({maxSeconds:1}),pcm=new Int16Array(321);f.session.setRecording(false);f.session.update(status());f.session.push(pcm,0);
  assert.equal(f.session.clip,null);f.session.setRecording(true);
  for(let n=1;n<=12;n++)f.session.push(pcm,n);
  f.session.setRecording(false);assert.equal(f.saved.length,2);
  assert.equal(f.saved.reduce((n,c)=>n+c.duration*3200,0),12*321);
  assert.equal(f.saved[0].reason,'Continued in next part');
});
test('PCM is copied before transfer to worklet and WAV is signed little endian', async () => {
  const f=fixture(),pcm=Int16Array.of(-32768,12345,32767);f.session.update(status());f.session.push(pcm,0);
  structuredClone(pcm,{transfer:[pcm.buffer]});f.session.reset();const v=new DataView(await f.saved[0].blob.arrayBuffer());
  assert.equal(v.getInt16(44,true),-32768);assert.equal(v.getInt16(46,true),12345);assert.equal(v.getInt16(48,true),32767);
  assert.equal(new TextDecoder().decode((await wavBlob([],6000).arrayBuffer()).slice(0,4)),'RIFF');
});
