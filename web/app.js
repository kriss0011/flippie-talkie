import {RATE,RX_RATE,SAMPLES,encode,decode,frame,Parser} from './codec.js';
import {BluetoothLink,USBLink} from './transports.js';
const $=id=>document.getElementById(id),sleep=ms=>new Promise(r=>setTimeout(r,ms));
let link,ready=false,connecting=false,status,lastStatus=0,heartbeat,monitor,ctx,output,playAt=0,sources=new Set(),micStream,micNode,micReady=false,held=false,transmitting=false,epoch=0,seq=0,encoder={index:0},pending=0,operation=0,rxSequence=null,received=0,lost=0,wakeLock;
for(let i=1;i<=16;i++)$('channel').add(new Option(String(i).padStart(2,'0'),i));$('channel').value='9';
const errors={1:'This channel is busy. Wait for the other radio to finish.',2:'This Flipper’s firmware does not allow transmission on this channel.',3:'Finish talking before changing channel.',4:'The radio could not start. Reconnect and try again.',5:'Audio stopped or the connection timed out. Press again to resume.',8:'An audio packet was damaged.',9:'The connection could not keep up. Move closer to the Flipper.'};
function notice(text=''){ $('notice').textContent=text;$('notice').hidden=!text; }
function clearPlayback(){for(const s of sources){try{s.stop();}catch{}}sources.clear();playAt=0;rxSequence=null;}
function render(){
  document.body.classList.toggle('connected',ready);
  const channel=status?.channel||Number($('channel').value),live=ready&&status;
  $('frequency').textContent=((446006250+(channel-1)*12500)/1e6).toFixed(5)+' MHz';$('ptt-detail').textContent='Channel '+channel;
  $('connection').textContent=connecting?'Connecting…':ready?'Connected':'Disconnected';$('device').textContent=ready?(link.device?.name||'Flipper via USB'):'Flipper radio';
  $('connect').textContent=connecting?'Cancel':ready?'Disconnect':'Connect Flipper';$('transport').disabled=ready||connecting;
  for(const id of ['channel','previous','next'])$(id).disabled=!live||held||status?.state>=3;
  $('listen').disabled=!live||held||status?.state>=3;$('listen').querySelector('span').textContent=live&&status.listening?'Pause listening':'Start listening';
  $('mic').disabled=!ready||held;$('mic').textContent=micReady?'Mute microphone':'Enable microphone';
  $('ptt').disabled=!ready||!micReady||(!held&&status?.state>=3);$('ptt').classList.toggle('active',held);
  $('ptt-label').textContent=held?(transmitting?'Talking…':'Opening channel…'):'Hold to talk';
  const names=['Listening paused','Listening','Receiving','Preparing microphone','Transmitting'];
  $('radio-state').textContent=live?names[status.state]||'Connected':'Connect to use your radio';$('state-dot').classList.toggle('live',!!live&&status.state>=2);
  $('signal').textContent=live&&status.rssi>-127?`${status.rssi} dBm`:'No signal reading';
  $('diagnostics').textContent=live?`${RX_RATE/1000} kHz receive / ${RATE/1000} kHz talk · Received ${received} blocks · Lost ${lost} · Device drops ${status.dropped} · Microphone gaps ${status.underruns}`:'No live connection.';
}
async function audio(){if(!ctx){ctx=new AudioContext({latencyHint:'interactive'});output=ctx.createGain();output.gain.value=Number($('volume').value)/100;output.connect(ctx.destination);}await ctx.resume();}
function receive(packet){
  if(packet.type===0x81||packet.type===0x82)lastStatus=performance.now();
  if(packet.type===0x81&&packet.data.length===26){
    const b=packet.data,v=new DataView(b.buffer,b.byteOffset,b.length);if(b[7]!==2||v.getUint16(10,true)!==RATE||v.getUint16(24,true)!==RX_RATE){notice('This app needs the matching PMR Connect version on the Flipper.');return;}
    const previousError=status?.error;status={channel:b[0],mode:b[1],state:b[2],listening:!!b[3],rssi:v.getInt8(4),epoch:b[5],error:b[6],buffered:v.getUint16(8,true),dropped:v.getUint32(12,true),underruns:v.getUint32(16,true)};lastStatus=performance.now();
    if(!ready){ready=true;connecting=false;notice();}
    $('channel').value=String(status.channel);
    if(status.error)notice(errors[status.error]||`Radio status ${status.error}`);else if(previousError)notice();
    if((status.state===0||status.state===1||status.state===4)&&sources.size&&status.state!==1)clearPlayback();
    if(transmitting&&status.epoch===epoch&&status.state<3)stopTalk(false);
    render();return;
  }
  if(packet.type!==0x82||!ready||!status?.listening||held||!ctx||ctx.state!=='running')return;
  try{
    const pcm=decode(packet.data),buffer=ctx.createBuffer(1,SAMPLES,RX_RATE),samples=buffer.getChannelData(0);for(let i=0;i<SAMPLES;i++)samples[i]=pcm[i]/32768;
    if(rxSequence!==null){const gap=(packet.sequence-rxSequence-1)&65535;if(gap>0&&gap<1000){lost+=gap;playAt+=Math.min(gap,3)*SAMPLES/RX_RATE;}}
    rxSequence=packet.sequence;received++;const now=ctx.currentTime;
    if(playAt<now+.015||playAt>now+.35){clearPlayback();rxSequence=packet.sequence;playAt=now+.10;}
    const source=ctx.createBufferSource();source.buffer=buffer;source.connect(output);source.onended=()=>sources.delete(source);sources.add(source);source.start(playAt);playAt+=SAMPLES/RX_RATE;
  }catch(e){notice(e.message);}
}
async function send(type,payload=new Uint8Array(),e=epoch){if(!link)throw Error('Connect your Flipper first.');return link.write(frame(type,payload,e,seq++));}
function lostConnection(){operation++;ready=false;connecting=false;held=false;transmitting=false;micNode?.port.postMessage(false);clearInterval(heartbeat);clearInterval(monitor);clearPlayback();status=null;releaseMic();wakeLock?.release().catch(()=>{});render();}
async function disconnect(){const old=link;stopTalk(false);try{await send(7);}catch{}link=null;lostConnection();try{await old?.close();}catch{}}
$('connect').onclick=async()=>{
  if(ready||connecting){await disconnect();return;}notice();connecting=true;render();const current=++operation;
  const selected=$('transport').value;link=selected==='bluetooth'?new BluetoothLink():new USBLink();const active=link;const parser=new Parser(receive);
  try{
    await audio();await active.open(bytes=>{if(link===active)parser.push(bytes);},()=>{if(link===active){link=null;lostConnection();notice('Flipper disconnected. Connect again when you are ready.');}});
    if(current!==operation){await active.close();return;}
    heartbeat=setInterval(()=>send(1).catch(()=>{}),400);await send(1);
    const until=performance.now()+8000;while(!ready&&performance.now()<until&&link===active)await sleep(50);
    if(!ready)throw Error('No reply from PMR Connect. Open the app on your Flipper and select the matching connection mode.');
    monitor=setInterval(()=>{if(ready&&performance.now()-lastStatus>2500){notice('Connection lost. Transmission has stopped.');disconnect();}},500);
    try{wakeLock=await navigator.wakeLock?.request('screen');}catch{}
  }catch(e){if(current!==operation)return;await disconnect();notice(e.name==='NotFoundError'?'No Flipper selected.':e.message);}
};
$('transport').onchange=()=>{$('connection-help').textContent=$('transport').value==='bluetooth'?'Open PMR Connect on your Flipper, select Bluetooth, then connect here.':'Connect a USB cable. Open PMR Connect on the Flipper and press OK to select USB.';};
async function setChannel(n){if(!ready||held)return;clearPlayback();notice();try{await send(3,Uint8Array.of(n));}catch(e){notice(e.message);}}
$('channel').onchange=()=>setChannel(Number($('channel').value));$('previous').onclick=()=>setChannel((status.channel+14)%16+1);$('next').onclick=()=>setChannel(status.channel%16+1);
$('listen').onclick=async()=>{try{await audio();notice();clearPlayback();await send(2,Uint8Array.of(status.listening?0:1));}catch(e){notice(e.message);}};
$('volume').oninput=()=>{const x=Number($('volume').value);$('volume-value').textContent=x+'%';if(output)output.gain.value=x/100;};
function releaseMic(){micNode?.disconnect();micNode=null;micStream?.getTracks().forEach(t=>t.stop());micStream=null;micReady=false;$('mic-help').textContent='Enable your microphone before talking.';setMeter(0);}
function setMeter(x){const n=Math.round(Math.min(100,x*200));$('meter').setAttribute('aria-valuenow',n);$('meter').firstElementChild.style.width=n+'%';}
$('mic').onclick=async()=>{
  if(micReady){releaseMic();render();return;}
  const current=operation;
  try{
    await audio();micStream=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true,noiseSuppression:true,autoGainControl:true},video:false});
    if(current!==operation||!ready){releaseMic();return;}
    await ctx.audioWorklet.addModule('./mic-worklet.js');
    const source=ctx.createMediaStreamSource(micStream),hp=ctx.createBiquadFilter(),lp=ctx.createBiquadFilter();hp.type='highpass';hp.frequency.value=200;lp.type='lowpass';lp.frequency.value=2400;
    micNode=new AudioWorkletNode(ctx,'radio-mic');const silent=ctx.createGain();silent.gain.value=0;source.connect(hp).connect(lp).connect(micNode).connect(silent).connect(ctx.destination);
    micNode.port.onmessage=({data})=>{
      if(!held||!transmitting)return;let power=0;for(const s of data)power+=(s/32768)**2;setMeter(Math.sqrt(power/data.length));
      if(pending>=3){stopTalk(false);notice('Bluetooth audio is falling behind. Move closer and try again.');return;}
      const payload=encode(data,encoder),audioEpoch=epoch;pending++;send(5,payload).catch(e=>{stopTalk(false);notice(e.message);}).finally(()=>{if(epoch===audioEpoch)pending--;});
    };
    micReady=true;$('mic-help').textContent='Ready. Audio is sent only while you hold to talk.';notice();render();
  }catch(e){releaseMic();notice('Microphone unavailable: '+e.message);render();}
};
async function startTalk(){
  if(held||!ready||!micReady||document.hidden)return;held=true;transmitting=false;epoch=(epoch+1)&255;const thisEpoch=epoch;encoder={index:0};pending=0;clearPlayback();notice();render();
  try{
    await audio();if(!held||thisEpoch!==epoch)return;
    await send(4,Uint8Array.of(status.channel),thisEpoch);
    const until=performance.now()+1000;
    while(held&&epoch===thisEpoch&&performance.now()<until){
      if(status?.epoch===thisEpoch&&status.state===3){transmitting=true;micNode.port.postMessage(true);render();return;}
      if(status?.error===1)break;await sleep(10);
    }
    if(held){stopTalk(false);notice(errors[status?.error]||'The radio did not open. Try again.');}
  }catch(e){stopTalk(false);notice(e.message);}
}
function stopTalk(drain=true){
  const wasHeld=held;held=false;transmitting=false;micNode?.port.postMessage(false);setMeter(0);
  if(wasHeld&&link)send(6,Uint8Array.of(drain?1:0)).catch(()=>{});render();
}
$('ptt').onpointerdown=e=>{if(e.button!==0)return;e.preventDefault();$('ptt').setPointerCapture(e.pointerId);startTalk();};
$('ptt').onpointerup=()=>stopTalk();$('ptt').onpointercancel=()=>stopTalk(false);$('ptt').onlostpointercapture=()=>stopTalk(false);$('ptt').oncontextmenu=e=>e.preventDefault();
window.addEventListener('keydown',e=>{if(e.code==='Space'&&!e.repeat&&!['INPUT','SELECT','TEXTAREA','BUTTON','SUMMARY','A'].includes(document.activeElement?.tagName)){e.preventDefault();startTalk();}else if(e.code==='Space'&&document.activeElement===$('ptt')&&!e.repeat){e.preventDefault();startTalk();}});
window.addEventListener('keyup',e=>{if(e.code==='Space'){e.preventDefault();stopTalk();}});
window.addEventListener('blur',()=>stopTalk(false));document.addEventListener('visibilitychange',()=>{if(document.hidden){stopTalk(false);clearPlayback();}});window.addEventListener('pagehide',()=>{stopTalk(false);send(7).catch(()=>{});});
if(!window.isSecureContext)notice('Open the HTTPS link to allow Bluetooth and your microphone.');
else if(!navigator.bluetooth){notice(/iPad|iPhone/.test(navigator.userAgent)?'On iPhone or iPad, open this page in Bluefy for Bluetooth.':'Bluetooth needs Chrome on this device. USB is also available in Chrome or Edge on computers.');}
render();
