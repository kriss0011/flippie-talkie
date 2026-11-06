import {RATE, RX_RATE, SAMPLES, encode, decode, frame, Parser} from './codec.js';
import {BluetoothLink, USBLink} from './transports.js';
import {ReceiveSession, DEFAULT_SQUELCH} from './receiver.js';
import {recordingLibrary} from './recordings-ui.js';
import {messagePanel} from './messages.js';
import {TX_LEAD_IN_MS, TX_LIMIT_MS, talkClock, waitForTxIdle} from './tx-timing.js';
import {AirHistory} from './air-history.js';
import {airDisplay, drawScope} from './pulse-field.js';
import {audioBar} from './audio-bar.js';
const $ = id => document.getElementById(id),
  sleep = ms => new Promise(r => setTimeout(r, ms));
let txStartedAt = 0,
  wasTalking = false,
  micScope = null;
let link,
  ready = false,
  connecting = false,
  status,
  lastStatus = 0,
  heartbeat,
  monitor,
  ctx,
  output,
  speaker,
  playback = {},
  micStream,
  micNode,
  micReady = false,
  held = false,
  transmitting = false,
  epoch = 0,
  seq = 0,
  encoder = {index: 0},
  pending = 0,
  operation = 0,
  rxSequence = null,
  received = 0,
  lost = 0,
  wakeLock,
  draftCapture = null,
  messageBusy = false,
  drainingEpoch = null,
  txReadyAt = 0;
for (let i = 1; i <= 16; i++) $('channel').add(new Option(String(i).padStart(2, '0'), i));
$('channel').value = '9';
let channelSquelch = {};
try {
  channelSquelch = JSON.parse(localStorage.getItem('flippie-squelch') || '{}') || {};
  if (typeof channelSquelch !== 'object' || Array.isArray(channelSquelch)) channelSquelch = {};
} catch {}
const receiver = new ReceiveSession({
  onAudio: (pcm, sequence) =>
    speaker?.port.postMessage({type: 'audio', pcm, sequence}, [pcm.buffer]),
  onReset: () => {
    speaker?.port.postMessage({type: 'reset'});
    rxSequence = null;
  },
  onEnd: () => speaker?.port.postMessage({type: 'end'}),
  onRecording: async clip => {
    const id = await library.save(clip);
    air.link(id, clip.startedAt, clip.duration);
  },
});
const air = new AirHistory();
audioBar($('recording-player'), 'Recording');
audioBar($('message-preview'), 'Message preview');
const library = recordingLibrary(enabled => receiver.setRecording(enabled));
const calm = matchMedia('(prefers-reduced-motion: reduce)');
const dbm = value => `${String(value).replace('-', '−')} dBm`;
const scalePercent = dbm => `${Math.max(0, Math.min(100, ((dbm + 111) / 71) * 100))}%`;
// The radio's lamp test: a one-off sweep of the meter that says the link is alive.
function lampTest() {
  if (calm.matches) return;
  $('signal-bars').animate(
    [{'--level': '0%'}, {'--level': '100%', offset: 0.4}, {'--level': '0%'}],
    {duration: 700, easing: 'ease-out'},
  );
}
function tickChannel(from, to) {
  if (calm.matches || !from) return;
  const step = (to - from + 16) % 16 < 8 ? 10 : -10;
  $('channel').animate(
    [
      {transform: `translateY(${step}px)`, opacity: 0.3},
      {transform: 'none', opacity: 1},
    ],
    {duration: 200, easing: 'cubic-bezier(0.16, 1, 0.3, 1)'},
  );
}
function loadSquelch(channel) {
  const saved = channelSquelch[channel],
    value =
      saved === null
        ? null
        : Number.isFinite(saved) && saved >= -110 && saved <= -40
          ? saved
          : DEFAULT_SQUELCH;
  $('squelch').value = value === null ? -111 : value;
  $('signal-scale').style.setProperty('--gate', scalePercent(Number($('squelch').value)));
  receiver.setThreshold(value);
  $('squelch-value').textContent = value === null ? 'Off' : dbm(value);
  $('squelch').setAttribute('aria-valuetext', value === null ? 'Off' : dbm(value));
}
$('squelch').oninput = () => {
  const value = Number($('squelch').value),
    channel = status?.channel || Number($('channel').value);
  channelSquelch[channel] = value === -111 ? null : value;
  loadSquelch(channel);
  try {
    localStorage.setItem('flippie-squelch', JSON.stringify(channelSquelch));
  } catch {}
  render();
};
loadSquelch(9);
airDisplay(air, {
  pulse: $('pulse'),
  received: $('wave-rx'),
  microphone: $('wave-mic'),
  strip: $('air-strip'),
  onPlay: id => library.play(id),
});
const messages = messagePanel({
  capture: async callback => {
    await enableMic();
    draftCapture = callback;
    micNode.port.postMessage(true);
    render();
  },
  endCapture: () => {
    draftCapture = null;
    micNode?.port.postMessage(false);
    render();
  },
  send: transmitRecorded,
  cancel: () => stopTalk(false),
  busy: value => {
    messageBusy = value;
    render();
  },
});
setInterval(() => {
  if (txReadyAt && performance.now() >= txReadyAt) {
    txReadyAt = 0;
    render();
  }
  if (held && txStartedAt) showTalkDetail();
  const was = receiver.audible;
  receiver.tick();
  library.update(receiver.clip);
  if (was !== receiver.audible) render();
}, 100);
const errors = {
  1: 'This channel is busy. Wait for the other radio to finish.',
  2: 'This Flipper’s firmware blocks transmitting on this channel. See “Allow transmitting on PMR446” in the README.',
  3: 'Finish talking before changing channel.',
  4: 'The radio could not start. Disconnect, reconnect and try again.',
  5: 'The audio link timed out. Hold to talk again, or reconnect if it keeps happening.',
  8: 'An audio packet was damaged. Try again.',
  9: 'The connection could not keep up. Move closer to the Flipper.',
};
const micError = e =>
  e.name === 'NotAllowedError'
    ? 'Microphone access is blocked. Allow it in your browser’s site settings, then try again.'
    : e.name === 'NotFoundError'
      ? 'No microphone found. Connect one, then try again.'
      : `The microphone could not start: ${e.message}`;
function notice(text = '') {
  $('notice').textContent = text;
  $('notice').hidden = !text;
}
function clearPlayback(reason = 'Reception stopped') {
  receiver.reset(reason);
  rxSequence = null;
}
function showTalkDetail() {
  const channel = status?.channel || Number($('channel').value);
  const talking = held && txStartedAt,
    elapsed = talking ? performance.now() - txStartedAt : 0;
  $('ptt-budget').style.setProperty('--left', talking ? 1 - elapsed / TX_LIMIT_MS : 0);
  $('ptt-detail').textContent = !ready
    ? 'Connect your Flipper first'
    : !micReady
      ? 'Then hold to talk'
      : talking
        ? `Channel ${channel} · ${talkClock(elapsed)}`
        : `Channel ${channel}`;
}
function render() {
  document.body.classList.toggle('connected', ready);
  const channel = status?.channel || Number($('channel').value),
    live = ready && status;
  $('frequency').textContent = ((446006250 + (channel - 1) * 12500) / 1e6).toFixed(5) + ' MHz';
  showTalkDetail();
  $('connection').textContent = connecting ? 'Connecting…' : ready ? 'Connected' : 'Not connected';
  $('device').textContent = ready ? link?.device?.name || 'USB' : '';
  $('connect').disabled = messageBusy;
  $('disconnect').disabled = messageBusy;
  $('disconnect').hidden = !ready;
  $('connect').textContent = connecting ? 'Cancel' : ready ? 'Disconnect' : 'Connect Flipper';
  $('transport').disabled = ready || connecting;
  for (const id of ['channel', 'previous', 'next'])
    $(id).disabled = !live || held || messageBusy || status?.state >= 3;
  $('listen').disabled = !live || held || messageBusy || status?.state >= 3;
  $('listen').querySelector('span').textContent =
    live && status.listening ? 'Pause listening' : 'Start listening';
  $('mic').disabled = held || messageBusy || !!draftCapture;
  // One line says what the microphone is doing now, so it can never contradict the radio.
  $('mic-help').textContent =
    messageBusy && held
      ? 'Sending your recorded message.'
      : held
        ? 'You’re on the air. Release to stop.'
        : drainingEpoch !== null
          ? 'Finishing your message.'
          : draftCapture
            ? 'Recording locally. Your message is not being transmitted.'
            : micReady
              ? 'Ready. Audio is sent only while you hold to talk.'
              : 'Enable your microphone before talking.';
  $('mic-label').textContent = micReady ? 'Turn off microphone' : 'Enable microphone';
  $('mic').classList.toggle('on', micReady);
  $('mic').setAttribute('aria-pressed', String(micReady));
  // Without a microphone the talk button enables it; it never transmits from that tap.
  $('ptt').disabled =
    !ready || messageBusy || !!draftCapture || (micReady && !held && status?.state >= 3);
  const talking = held && transmitting && !txReadyAt && status?.state === 4;
  $('ptt').classList.toggle('active', held);
  $('ptt').classList.toggle('opening', held && !!txReadyAt);
  $('ptt').classList.toggle('talking', talking);
  // A tick in the hand says the channel is open without looking at the screen.
  if (talking && !wasTalking && !messageBusy) navigator.vibrate?.(20);
  wasTalking = talking;
  $('ptt-label').textContent = held
    ? talking
      ? 'Talking…'
      : 'Opening channel…'
    : drainingEpoch !== null
      ? 'Finishing message…'
      : ready && !micReady
        ? 'Enable microphone'
        : 'Hold to talk';
  const names = ['Paused', 'Listening', 'Receiving', 'Opening channel…', 'Transmitting'];
  $('radio-state').textContent = live
    ? status.state === 2 && !receiver.audible
      ? 'Signal below squelch'
      : names[status.state] || 'Connected'
    : 'Connect to use your radio';
  const onAir = !!live && ((status.state === 2 && receiver.audible) || status.state >= 3);
  $('state-dot').classList.toggle('live', onAir);
  document.body.classList.toggle('sending', !!live && status.state >= 3);
  const [linked, listening, receiving, sending] = $('state-strip').children;
  linked.classList.toggle('on', ready);
  listening.classList.toggle('on', !!live && !!status.listening);
  receiving.classList.toggle('on', !!live && status.state === 2 && receiver.audible);
  sending.classList.toggle('on', !!live && status.state >= 3);
  $('channel-box').classList.toggle('live', onAir);
  const reading = live && status.rssi > -127;
  $('signal').textContent = reading ? dbm(status.rssi) : 'No reading';
  $('signal-scale').style.setProperty('--level', reading ? scalePercent(status.rssi) : '0%');
  messages?.setConnected(ready, held);
  $('diagnostics').textContent = live
    ? `${(status.rxRate || RX_RATE) / 1000} kHz receive / ${RATE / 1000} kHz talk · Received ${received} blocks · Lost ${lost} · Device drops ${status.dropped} · Playback gaps ${playback.underruns || 0} · Buffer ${playback.bufferedMs || 0} ms · Microphone gaps ${status.underruns}`
    : 'No live connection.';
}
async function audio() {
  if (!ctx) {
    ctx = new AudioContext({latencyHint: 'interactive'});
    output = ctx.createGain();
    output.gain.value = Number($('volume').value) / 100;
    output.connect(ctx.destination);
  }
  await ctx.resume();
  if (!speaker) {
    await ctx.audioWorklet.addModule('./speaker-worklet.js');
    speaker = new AudioWorkletNode(ctx, 'radio-speaker', {
      numberOfInputs: 0,
      numberOfOutputs: 1,
      outputChannelCount: [1],
    });
    speaker.connect(output);
    speaker.port.onmessage = ({data}) => {
      playback = data;
    };
  }
}
function receive(packet) {
  if (packet.type === 0x81 || packet.type === 0x82) lastStatus = performance.now();
  if (packet.type === 0x81 && packet.data.length === 26) {
    const b = packet.data,
      v = new DataView(b.buffer, b.byteOffset, b.length);
    if (
      ![2, 3].includes(b[7]) ||
      v.getUint16(10, true) !== RATE ||
      ![3200, 4000].includes(v.getUint16(24, true))
    ) {
      notice(
        'This page doesn’t match the Flippie Talkie version on your Flipper. Update the Flipper app.',
      );
      return;
    }
    const previousError = status?.error,
      previousState = status?.state,
      previousChannel = status?.channel;
    status = {
      rxRate: v.getUint16(24, true),
      channel: b[0],
      mode: b[1],
      state: b[2],
      listening: !!b[3],
      rssi: v.getInt8(4),
      epoch: b[5],
      error: b[6],
      buffered: v.getUint16(8, true),
      dropped: v.getUint32(12, true),
      underruns: v.getUint32(16, true),
    };
    lastStatus = performance.now();
    if (!ready) {
      ready = true;
      lampTest();
      connecting = false;
      notice();
    }
    $('channel').value = String(status.channel);
    if (previousChannel !== status.channel) {
      loadSquelch(status.channel);
      tickChannel(previousChannel, status.channel);
    }
    if (status.error) notice(errors[status.error] || `Radio status ${status.error}`);
    else if (previousError) notice();
    speaker?.port.postMessage({type: 'rate', rate: status.rxRate});
    if (document.hidden) receiver.reset('Page moved to background');
    else receiver.update(status);
    if (status.state === 2 && previousState !== 2) speaker?.port.postMessage({type: 'begin'});
    if (status.epoch === epoch && status.state === 4 && previousState !== 4)
      txReadyAt = performance.now() + TX_LEAD_IN_MS;
    if (status.state !== 4) txStartedAt = 0;
    else if (previousState !== 4) txStartedAt = performance.now();
    if (status.epoch === epoch && status.state < 3) {
      drainingEpoch = null;
      txReadyAt = 0;
      if (transmitting) stopTalk(false);
    }
    render();
    return;
  }
  if (
    packet.type !== 0x82 ||
    !ready ||
    !status?.listening ||
    held ||
    !ctx ||
    ctx.state !== 'running'
  )
    return;
  try {
    const pcm = decode(packet.data);
    if (rxSequence !== null) {
      const gap = (packet.sequence - rxSequence - 1) & 65535;
      if (gap > 0 && gap < 1000) lost += gap;
    }
    rxSequence = packet.sequence;
    received++;
    air.push('rx', pcm, status.rxRate || RX_RATE, Date.now(), {gated: !receiver.audible});
    receiver.push(pcm, packet.sequence);
  } catch (e) {
    notice(e.message);
  }
}
async function send(type, payload = new Uint8Array(), e = epoch) {
  if (!link) throw Error('Connect your Flipper first.');
  return link.write(frame(type, payload, e, seq++));
}
function lostConnection() {
  messages.stop('Flipper disconnected.');
  drainingEpoch = null;
  txReadyAt = 0;
  operation++;
  ready = false;
  connecting = false;
  held = false;
  transmitting = false;
  micNode?.port.postMessage(false);
  clearInterval(heartbeat);
  clearInterval(monitor);
  clearPlayback();
  status = null;
  releaseMic();
  wakeLock?.release().catch(() => {});
  render();
}
async function disconnect() {
  const old = link;
  stopTalk(false);
  try {
    await send(7);
  } catch {}
  link = null;
  lostConnection();
  try {
    await old?.close();
  } catch {}
}
$('connect').onclick = async () => {
  if (ready || connecting) {
    await disconnect();
    return;
  }
  notice();
  connecting = true;
  render();
  const current = ++operation;
  const selected = $('transport').value;
  link = selected === 'bluetooth' ? new BluetoothLink() : new USBLink();
  const active = link;
  const parser = new Parser(receive);
  try {
    await audio();
    await active.open(
      bytes => {
        if (link === active) parser.push(bytes);
      },
      () => {
        if (link === active) {
          link = null;
          lostConnection();
          notice('Flipper disconnected. Connect again when you are ready.');
        }
      },
    );
    if (current !== operation) {
      await active.close();
      return;
    }
    heartbeat = setInterval(() => send(1).catch(() => {}), 400);
    await send(1);
    const until = performance.now() + 8000;
    while (!ready && performance.now() < until && link === active) await sleep(50);
    if (!ready)
      throw Error(
        'No reply from the Flipper. Open Browser link in Flippie Talkie and check that Settings → Connection matches this page.',
      );
    monitor = setInterval(() => {
      if (ready && performance.now() - lastStatus > 2500) {
        notice('Connection lost, so transmitting stopped. Connect again when you’re ready.');
        disconnect();
      }
    }, 500);
    try {
      wakeLock = await navigator.wakeLock?.request('screen');
    } catch {}
  } catch (e) {
    if (current !== operation) return;
    await disconnect();
    notice(
      e.name === 'NotFoundError'
        ? 'No Flipper selected. Select Connect Flipper to try again.'
        : e.message,
    );
  }
};
$('disconnect').onclick = disconnect;
$('transport').onchange = () => {
  const usb = $('transport').value === 'usb';
  $('steps-usb').hidden = !usb;
  $('steps-bluetooth').hidden = usb;
};
async function setChannel(n) {
  if (!ready || held || messageBusy) return;
  clearPlayback('Channel changed');
  notice();
  try {
    await send(3, Uint8Array.of(n));
  } catch (e) {
    notice(e.message);
  }
}
$('channel').onchange = () => setChannel(Number($('channel').value));
$('previous').onclick = () => setChannel(((status.channel + 14) % 16) + 1);
$('next').onclick = () => setChannel((status.channel % 16) + 1);
$('listen').onclick = async () => {
  try {
    await audio();
    notice();
    clearPlayback();
    await send(2, Uint8Array.of(status.listening ? 0 : 1));
  } catch (e) {
    notice(e.message);
  }
};
const showVolume = () => $('volume').style.setProperty('--fill', $('volume').value + '%');
showVolume();
$('volume').oninput = () => {
  showVolume();
  const x = Number($('volume').value);
  $('volume-value').textContent = x + '%';
  if (output) output.gain.value = x / 100;
};
function releaseMic() {
  micScope = null;
  drawScope($('ptt-scope'), null);
  micNode?.disconnect();
  micNode = null;
  micStream?.getTracks().forEach(t => t.stop());
  micStream = null;
  micReady = false;
  setMeter(0);
}
function setMeter(x) {
  const n = Math.round(Math.min(100, x * 200));
  $('meter').setAttribute('aria-valuenow', n);
  $('meter').firstElementChild.style.width = n + '%';
}
// Reads the microphone for display only. Speech reaches the radio through the worklet.
const SCOPE_RATE = 6000;
function watchMic() {
  if (!micScope || messageBusy) return;
  const samples = new Float32Array(micScope.fftSize);
  micScope.getFloatTimeDomainData(samples);
  const stride = Math.round(ctx.sampleRate / SCOPE_RATE),
    wanted = Math.round(SCOPE_RATE * 0.05),
    pcm = new Int16Array(wanted),
    start = samples.length - wanted * stride;
  for (let i = 0; i < wanted; i++) {
    pcm[i] = Math.max(-1, Math.min(1, samples[Math.max(0, start + i * stride)])) * 32767;
  }
  if (!calm.matches) drawScope($('ptt-scope'), samples);
  const onAir = held && transmitting;
  air.push('mic', pcm, SCOPE_RATE, Date.now(), {kind: onAir ? 'tx' : 'mic', log: onAir});
}
setInterval(watchMic, 50);
let micSetup;
async function enableMic() {
  if (micReady) return;
  micSetup ??= buildMic().finally(() => {
    micSetup = null;
  });
  return micSetup;
}
async function buildMic() {
  if (micReady) return;
  const current = operation;
  try {
    await audio();
    micStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        channelCount: 1,
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true,
      },
      video: false,
    });
    if (current !== operation) {
      releaseMic();
      throw Error('Connection changed. Try again.');
    }
    await ctx.audioWorklet.addModule('./mic-worklet.js');
    if (current !== operation) throw Error('Connection changed. Try again.');
    const source = ctx.createMediaStreamSource(micStream),
      hp = ctx.createBiquadFilter(),
      lp = ctx.createBiquadFilter();
    hp.type = 'highpass';
    hp.frequency.value = 200;
    lp.type = 'lowpass';
    lp.frequency.value = 2400;
    micNode = new AudioWorkletNode(ctx, 'radio-mic');
    micScope = ctx.createAnalyser();
    micScope.fftSize = 2048;
    lp.connect(micScope);
    const silent = ctx.createGain();
    silent.gain.value = 0;
    source.connect(hp).connect(lp).connect(micNode).connect(silent).connect(ctx.destination);
    micNode.port.onmessage = ({data}) => {
      if (draftCapture) {
        draftCapture(data);
        return;
      }
      if (!held || !transmitting) return;
      let power = 0;
      for (const s of data) power += (s / 32768) ** 2;
      setMeter(Math.sqrt(power / data.length));
      if (pending >= 3) {
        stopTalk(false);
        notice(
          'Audio is falling behind, so transmitting stopped. Move closer to the Flipper and try again.',
        );
        return;
      }
      const payload = encode(data, encoder),
        audioEpoch = epoch;
      pending++;
      send(5, payload)
        .catch(e => {
          stopTalk(false);
          notice(e.message);
        })
        .finally(() => {
          if (epoch === audioEpoch) pending--;
        });
    };
    micReady = true;
    notice();
    render();
  } catch (e) {
    releaseMic();
    render();
    throw e;
  }
}
$('mic').onclick = async () => {
  if (micReady) {
    releaseMic();
    render();
    return;
  }
  try {
    await enableMic();
  } catch (e) {
    notice(micError(e));
  }
};
async function startTalk(recorded = false) {
  if (
    held ||
    !ready ||
    (!recorded && !micReady) ||
    document.hidden ||
    draftCapture ||
    (!recorded && messageBusy)
  )
    return;
  held = true;
  transmitting = false;
  epoch = (epoch + 1) & 255;
  const thisEpoch = epoch;
  encoder = {index: 0};
  pending = 0;
  clearPlayback();
  notice();
  render();
  try {
    await audio();
    if (!held || thisEpoch !== epoch) return;
    await send(4, Uint8Array.of(status.channel), thisEpoch);
    const until = performance.now() + 1000;
    while (held && epoch === thisEpoch && performance.now() < until) {
      if (status?.epoch === thisEpoch && status.state === 3) {
        transmitting = true;
        if (!recorded) micNode.port.postMessage(true);
        render();
        return true;
      }
      if (status?.error === 1) break;
      await sleep(10);
    }
    if (held) {
      stopTalk(false);
      notice(errors[status?.error] || 'The radio did not open. Try again.');
    }
  } catch (e) {
    stopTalk(false);
    notice(e.message);
  }
}
function stopTalk(drain = true) {
  const wasHeld = held,
    forceStop =
      !drain && (drainingEpoch === epoch || (status?.epoch === epoch && status?.state >= 3));
  held = false;
  transmitting = false;
  txReadyAt = 0;
  if (!draftCapture) micNode?.port.postMessage(false);
  setMeter(0);
  if ((wasHeld || forceStop) && link) {
    drainingEpoch = drain ? epoch : null;
    send(6, Uint8Array.of(drain ? 1 : 0)).catch(() => {});
  }
  render();
}
async function transmitRecorded(pcm, signal) {
  if (!ready || !link) throw Error('Connect your Flipper before sending.');
  if (status?.state === 2) {
    const e = Error('Channel busy — waiting for it to clear.');
    e.name = 'ChannelBusyError';
    throw e;
  }
  const abort = () => stopTalk(false);
  signal.addEventListener('abort', abort, {once: true});
  try {
    if (signal.aborted) throw Error('Stopped.');
    if (!(await startTalk(true))) {
      const e = Error(errors[status?.error] || 'The radio did not open.');
      if (status?.error === 1) e.name = 'ChannelBusyError';
      throw e;
    }
    const thisEpoch = epoch,
      start = performance.now();
    let n = 0;
    for (let offset = 0; offset < pcm.length; offset += SAMPLES) {
      if (signal.aborted || !ready || !held || epoch !== thisEpoch)
        throw Error('Transmission stopped.');
      const block = new Int16Array(SAMPLES);
      block.set(pcm.subarray(offset, offset + SAMPLES));
      await send(5, encode(block, encoder), thisEpoch);
      air.push('mic', block, RATE, Date.now(), {kind: 'tx'});
      n++;
      const delay = start + ((n * SAMPLES) / RATE) * 1000 - performance.now();
      if (delay < -250)
        throw Error('Audio delivery fell behind. Move closer to the Flipper and try again.');
      if (delay > 0) await sleep(delay);
    }
    stopTalk(true);
    await waitForTxIdle({epoch: thisEpoch, getStatus: () => status, isReady: () => ready, signal});
  } finally {
    signal.removeEventListener('abort', abort);
    if (held || drainingEpoch !== null) stopTalk(false);
  }
}
$('ptt').onclick = async () => {
  if (micReady || !ready) return;
  try {
    await enableMic();
  } catch (e) {
    notice(micError(e));
  }
};
$('ptt').onpointerdown = e => {
  if (e.button !== 0) return;
  e.preventDefault();
  $('ptt').setPointerCapture(e.pointerId);
  startTalk();
};
$('ptt').onpointerup = () => stopTalk();
$('ptt').onpointercancel = () => stopTalk(false);
$('ptt').onlostpointercapture = () => {
  if (held) stopTalk(false);
};
$('ptt').oncontextmenu = e => e.preventDefault();
window.addEventListener('keydown', e => {
  if (
    e.code === 'Space' &&
    !e.repeat &&
    !['INPUT', 'SELECT', 'TEXTAREA', 'BUTTON', 'SUMMARY', 'A'].includes(
      document.activeElement?.tagName,
    )
  ) {
    e.preventDefault();
    startTalk();
  } else if (e.code === 'Space' && document.activeElement === $('ptt') && !e.repeat) {
    e.preventDefault();
    startTalk();
  }
});
window.addEventListener('keyup', e => {
  if (e.code === 'Space' && !messageBusy) {
    e.preventDefault();
    stopTalk();
  }
});
window.addEventListener('blur', () => {
  messages.stop('Stopped when this page lost focus.', false);
  stopTalk(false);
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    messages.stop('Stopped in the background.');
    stopTalk(false);
    clearPlayback('Page moved to background');
  }
});
window.addEventListener('pagehide', () => {
  messages.stop('Page closed.');
  clearPlayback('Page closed');
  stopTalk(false);
  send(7).catch(() => {});
});
if (!window.isSecureContext)
  notice('Open this page over HTTPS or localhost to use Bluetooth and the microphone.');
else if (!navigator.bluetooth) {
  notice(
    /iPad|iPhone/.test(navigator.userAgent)
      ? 'On iPhone or iPad, open this page in Bluefy for Bluetooth.'
      : 'This browser can’t use Bluetooth. Use Chrome, or Chrome or Edge on a computer for USB.',
  );
}
render();
