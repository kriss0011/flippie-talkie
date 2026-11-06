import test from 'node:test';import assert from 'node:assert/strict';import {execFileSync} from 'node:child_process';import {encode,decode,frame,Parser,SAMPLES,BYTES} from '../web/codec.js';import {mkdtempSync,writeFileSync} from 'node:fs';import {tmpdir} from 'node:os';import {join,resolve} from 'node:path';
const temp=mkdtempSync(join(tmpdir(),'pmr-codec-')),bin=join(temp,'codec');
execFileSync(process.env.CC || 'cc',['-std=c11','-Wall','-Wextra','-Werror','-I',resolve('firmware/flippie_talkie'),'tests/codec_bridge.c','-o',bin]);
const pcm=Int16Array.from({length:SAMPLES*30},(_,i)=>Math.round(Math.sin(i*2*Math.PI*710/6000)*22000+Math.sin(i*.071)*7000));
let blocks=[],state={index:0};for(let i=0;i<pcm.length;i+=SAMPLES)blocks.push(encode(pcm.subarray(i,i+SAMPLES),state));const encoded=Buffer.concat(blocks);
test('C and JavaScript encode and decode match sample for sample',()=>{
 assert.deepEqual(execFileSync(bin,{input:Buffer.from(pcm.buffer)}),encoded);
 const native=execFileSync(bin,['decode'],{input:encoded}),js=Buffer.concat(blocks.map(b=>Buffer.from(decode(b).buffer)));assert.deepEqual(native,js);
});
test('Independent FFmpeg IMA WAV decoder agrees',()=>{
 const fmt=Buffer.alloc(20);fmt.writeUInt16LE(17,0);fmt.writeUInt16LE(1,2);fmt.writeUInt32LE(6000,4);fmt.writeUInt32LE(Math.round(6000*BYTES/SAMPLES),8);fmt.writeUInt16LE(BYTES,12);fmt.writeUInt16LE(4,14);fmt.writeUInt16LE(2,16);fmt.writeUInt16LE(SAMPLES,18);
 function chunk(name,data){const h=Buffer.alloc(8);h.write(name);h.writeUInt32LE(data.length,4);return Buffer.concat([h,data]);}
 const fact=Buffer.alloc(4);fact.writeUInt32LE(pcm.length);const body=Buffer.concat([Buffer.from('WAVE'),chunk('fmt ',fmt),chunk('fact',fact),chunk('data',encoded)]),head=Buffer.alloc(8);head.write('RIFF');head.writeUInt32LE(body.length,4);const path=join(temp,'ima.wav');writeFileSync(path,Buffer.concat([head,body]));
 const ff=execFileSync(process.env.FFMPEG || 'ffmpeg',['-v','error','-i',path,'-f','s16le','-acodec','pcm_s16le','-']),ours=execFileSync(bin,['decode'],{input:encoded});
 assert.equal(ff.length,ours.length);
 // IMA decoders round differently: FFmpeg versions differ from ours by up to 2 per
 // sample, so the gap can grow by 2 per sample but restarts at each block's exact header.
 for(let i=0;i<ours.length/2;i++){const gap=Math.abs(ff.readInt16LE(i*2)-ours.readInt16LE(i*2));assert.ok(gap<=2*(i%SAMPLES),`Sample ${i} differs by ${gap}`);}
});
test('Parser handles arbitrary BLE/USB splits and noise without losing frames',()=>{
 const packets=[];const parser=new Parser(p=>packets.push(p));const wire=Buffer.concat([Buffer.from('CLI prompt\r\n'),frame(0x82,blocks[0],4,65535),frame(0x81,Uint8Array.of(1,2,3))]);
 for(let i=0;i<wire.length;i+=7)parser.push(wire.subarray(i,i+7));assert.equal(packets.length,2);assert.equal(packets[0].sequence,65535);assert.deepEqual(packets[0].data,blocks[0]);
});
test('Malformed audio is rejected',()=>{let b=blocks[0].slice();b[2]=89;assert.throws(()=>decode(b));b=blocks[0].slice();b[3]=1;assert.throws(()=>decode(b));assert.throws(()=>decode(b.subarray(1)));});
