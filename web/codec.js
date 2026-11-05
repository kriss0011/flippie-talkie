export const RATE=6000, RX_RATE=4000, SAMPLES=321, BYTES=164;
const steps=[7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767];
const indices=[-1,-1,-1,-1,2,4,6,8], clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
function nibble(code,s){const step=steps[s.index];let d=step>>3;if(code&1)d+=step>>2;if(code&2)d+=step>>1;if(code&4)d+=step;s.predictor=clamp(s.predictor+((code&8)?-d:d),-32768,32767);s.index=clamp(s.index+indices[code&7],0,88);return s.predictor;}
export function encode(pcm,state={index:0}){
  if(pcm.length!==SAMPLES)throw Error('Audio block length');
  const b=new Uint8Array(BYTES),v=new DataView(b.buffer),s={predictor:pcm[0],index:clamp(state.index,0,88)};
  v.setInt16(0,s.predictor,true);b[2]=s.index;
  for(let i=1;i<SAMPLES;i++){let d=pcm[i]-s.predictor,step=steps[s.index],code=0;if(d<0){code=8;d=-d;}if(d>=step){code|=4;d-=step;}step>>=1;if(d>=step){code|=2;d-=step;}step>>=1;if(d>=step)code|=1;nibble(code,s);b[4+((i-1)>>1)]|=code<<((i&1)?0:4);}
  state.index=s.index;return b;
}
export function decode(b){
  if(b.length!==BYTES||b[2]>88||b[3])throw Error('Invalid audio block');
  const s={predictor:new DataView(b.buffer,b.byteOffset,b.byteLength).getInt16(0,true),index:b[2]},pcm=new Int16Array(SAMPLES);pcm[0]=s.predictor;
  for(let i=1;i<SAMPLES;i++)pcm[i]=nibble((b[4+((i-1)>>1)]>>((i&1)?0:4))&15,s);return pcm;
}
export function frame(type,payload=new Uint8Array(),epoch=0,sequence=0){
  if(payload.length>BYTES)throw Error('Packet too long');const b=new Uint8Array(8+payload.length),v=new DataView(b.buffer);b.set([80,82,type,epoch]);v.setUint16(4,payload.length,true);v.setUint16(6,sequence,true);b.set(payload,8);return b;
}
export class Parser{
  constructor(onPacket){this.onPacket=onPacket;this.pending=new Uint8Array();}
  push(bytes){const b=new Uint8Array(this.pending.length+bytes.length);b.set(this.pending);b.set(bytes,this.pending.length);let i=0;
    while(i+8<=b.length){if(b[i]!==80||b[i+1]!==82){i++;continue;}const n=b[i+4]|(b[i+5]<<8);if(n>BYTES){i++;continue;}if(i+8+n>b.length)break;this.onPacket({type:b[i+2],epoch:b[i+3],sequence:b[i+6]|(b[i+7]<<8),data:b.slice(i+8,i+8+n)});i+=8+n;}
    this.pending=b.slice(i);if(this.pending.length>180)this.pending=new Uint8Array();
  }
}
