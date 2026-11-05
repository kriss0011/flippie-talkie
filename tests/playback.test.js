import test from 'node:test';
import assert from 'node:assert/strict';
import {RadioPlayback} from '../web/playback-buffer.js';

for (const rate of [3200,4000]) for (const outputRate of [44100,48000]) {
  test(`Continuous ${rate} Hz receive at ${outputRate} Hz survives recurring 180 ms delivery stalls`, () => {
    const player = new RadioPlayback(rate, outputRate), arrivals = [];
    let previous = 0;
    const duration = 40, block = 321, period = block / rate;
    for (let n = 0; n < duration / period; n++) {
      const produced = (n + 1) * period;
      const jitter = n % 31 === 20 ? .18 : .006 + (n % 5) * .007;
      previous = Math.max(previous + .001, produced + jitter);
      arrivals.push({at:previous,sequence:n,pcm:Int16Array.from({length:block},(_,i)=>Math.round(14000*Math.sin((n*block+i)*2*Math.PI*400/rate)))});
    }
    let next = 0, audible = 0;
    const output = new Float32Array(128);
    for (let now = 0; now < duration; now += output.length / outputRate) {
      while (next < arrivals.length && arrivals[next].at <= now) {
        const p = arrivals[next++]; player.push(p.pcm,p.sequence);
      }
      player.render(output);
      if (now > 1) {
        assert.ok(output.some(v=>Math.abs(v)>.03),`unexpected silent render at ${now}`);
        audible++;
      }
    }
    assert.ok(audible > 1000);
    assert.equal(player.underruns,0);
    assert.equal(player.overruns,0);
    assert.equal(player.lost,0);
  });
}

for (const drift of [-.003,.003]) test(`Clock mismatch ${drift} stays bounded for two minutes`,()=>{
  const rate=3200,outRate=48000,player=new RadioPlayback(rate,outRate),output=new Float32Array(128);
  const pcm=new Int16Array(321).fill(12000);let n=0;
  for(let now=0;now<120;now+=128/outRate){
    while((n+1)*321/rate*(1+drift)<now){player.push(pcm,n++);}
    player.render(output);
  }
  assert.equal(player.underruns,0);assert.equal(player.overruns,0);
  assert.ok(player.stats().bufferedMs<500);
});

test('Packet loss is counted, duplicates ignored and short tails drain',()=>{
  const p=new RadioPlayback(3200,48000),pcm=new Int16Array(321).fill(10000);
  p.push(pcm,65534);p.push(pcm,0);p.push(pcm,0);p.push(pcm,65535);
  assert.equal(p.lost,1);assert.equal(p.late,2);
  p.end();const output=new Float32Array(128);
  for(let i=0;i<200;i++)p.render(output);
  assert.equal(p.stats().bufferedMs,0);assert.equal(p.underruns,0);
  p.reset();p.end();p.push(pcm,1);
  p.render(output);assert.ok(output.some(x=>x>0),'sub-target tail plays');
});
