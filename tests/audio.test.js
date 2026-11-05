import test from 'node:test';import assert from 'node:assert/strict';import vm from 'node:vm';import fs from 'node:fs';
for(const rate of [44100,48000])test(`Microphone resamples ${rate} Hz continuously to 6000 Hz`,()=>{
 const blocks=[];let Processor;class Base{constructor(){this.port={postMessage:x=>blocks.push(x),onmessage:null};}}
 const sandbox={AudioWorkletProcessor:Base,sampleRate:rate,registerProcessor:(name,p)=>Processor=p};vm.runInNewContext(fs.readFileSync('web/mic-worklet.js','utf8'),sandbox);
 const p=new Processor();p.port.onmessage({data:true});const count=rate*3;
 for(let pos=0;pos<count;pos+=128){const a=Float32Array.from({length:Math.min(128,count-pos)},(_,i)=>.4*Math.sin((pos+i)*2*Math.PI*700/rate));p.process([[a]]);}
 assert.equal(blocks.length,Math.floor(18000/321));assert.equal(blocks.length*321+p.count,18000);assert.ok(blocks.flatMap(b=>[...b]).every(x=>Math.abs(x)<=13107));
 p.port.onmessage({data:false});const old=blocks.length;p.process([[new Float32Array(128)]]);assert.equal(blocks.length,old);assert.equal(p.count,0);
});
