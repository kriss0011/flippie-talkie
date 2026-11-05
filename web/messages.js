import {RecordingStore} from './recording-store.js';
import {wavBlob} from './receiver.js';
import {repeatMessage} from './broadcast.js';
const $ = id => document.getElementById(id), RATE = 6000;
export function messagePanel(hooks) {
  const store = new RecordingStore();
  let pcm = null, previewURL, recording = false, preparing = false, connected = false, manualTalk = false, controller = null, chunks = [], samples = 0, generation = 0;
  const info = text => { $('message-status').textContent = text; };
  function render() {
    const busy = preparing || recording || !!controller;
    $('record-message').disabled = preparing || !!controller || manualTalk;
    $('record-message').textContent = recording ? 'Finish recording' : 'Record message';
    $('send-message').disabled = !connected || !pcm || busy || manualTalk;
    $('stop-message').disabled = !busy;
    for (const id of ['repeat-mode', 'repeat-count', 'repeat-duration', 'repeat-duration-unit', 'repeat-gap', 'repeat-gap-unit']) $(id).disabled = busy;
    $('repeat-count-row').hidden = $('repeat-mode').value !== 'count';
    $('repeat-duration-row').hidden = $('repeat-mode').value !== 'duration';
  }
  function preview(blob) {
    const audio = $('message-preview'); audio.pause();
    if (previewURL) URL.revokeObjectURL(previewURL);
    previewURL = URL.createObjectURL(blob); audio.src = previewURL;
  }
  async function finishCapture() {
    if (!recording) return;
    recording = false; hooks.endCapture(); hooks.busy(false);
    if (samples < RATE / 2) { info('That was too short. Record at least half a second.'); render(); return; }
    pcm = new Int16Array(samples); let offset = 0;
    for (const chunk of chunks) { pcm.set(chunk, offset); offset += chunk.length; }
    chunks = []; const blob = wavBlob([pcm], RATE); preview(blob); render();
    info(`Message ready · ${(pcm.length / RATE).toFixed(1)} seconds. Preview it before sending.`);
    try { await store.save({id: 'outgoing-message', kind: 'message', rate: RATE, startedAt: Date.now(), bytes: blob.size, blob}); }
    catch { info('Message ready for this session. Browser storage could not keep it after reload.'); }
  }
  $('record-message').onclick = async () => {
    if (recording) { await finishCapture(); return; }
    const current = ++generation; preparing = true; chunks = []; samples = 0; hooks.busy(true); render();
    try {
      await hooks.capture(data => {
        if (!recording) return;
        const remaining = RATE * 45 - samples, part = data.slice(0, Math.min(data.length, remaining));
        chunks.push(part); samples += part.length;
        info(`Recording your message · ${(samples / RATE).toFixed(1)} / 45 seconds`);
        if (samples >= RATE * 45) finishCapture();
      });
      if (current !== generation) { hooks.endCapture(); return; }
      recording = true; info('Recording your message. This is not being transmitted.');
    } catch (error) { info('Microphone unavailable: ' + error.message); hooks.busy(false); }
    finally { preparing = false; render(); }
  };
  $('repeat-mode').onchange = render;
  $('send-message').onclick = async () => {
    if (controller || !connected || !pcm) return;
    controller = new AbortController(); const active = controller; let deadlineTimer;
    $('message-preview').pause(); hooks.busy(true); render();
    try {
      const durationMs = $('repeat-mode').value === 'duration' ? Number($('repeat-duration').value) * Number($('repeat-duration-unit').value) : null;
      if(Number.isFinite(durationMs)&&durationMs>=1000&&durationMs<=3600000)deadlineTimer=setTimeout(()=>{active.abort();hooks.cancel();},durationMs);
      const sent = await repeatMessage({pcm, signal: active.signal, count: Number($('repeat-count').value), durationMs,
        gapMs: Number($('repeat-gap').value) * Number($('repeat-gap-unit').value), send: hooks.send,
        progress: state => info(state.state === 'sending' ? `Sending message ${state.sent + 1}${durationMs === null ? ' of ' + state.count : ''}…` : state.state === 'busy' ? 'Channel busy. Waiting before trying again.' : `Sent ${state.sent}${durationMs === null ? ' of ' + state.count : ''}. Waiting for the next send…`)});
      info(active.signal.aborted ? `Stopped. ${sent} complete ${sent === 1 ? 'send' : 'sends'}.` : `Finished. ${sent} ${sent === 1 ? 'message' : 'messages'} sent.`);
    } catch (error) { if (!active.signal.aborted) info(error.message); }
    finally { clearTimeout(deadlineTimer); if (controller === active) controller = null; hooks.busy(false); render(); }
  };
  function stop(reason = 'Stopped.', includeCapture = true) {
    if (controller) { controller.abort(); hooks.cancel(); info(reason); }
    if (includeCapture && (recording || preparing)) {
      generation++; if (recording) finishCapture(); else { preparing = false; hooks.endCapture(); hooks.busy(false); }
      info(reason); render();
    }
  }
  $('stop-message').onclick = () => stop();
  store.audio('outgoing-message').then(async blob => {
    if (!blob || pcm || recording || preparing) return;
    const data = new DataView(await blob.arrayBuffer());
    if (data.byteLength < 44 || data.getUint32(24, true) !== RATE) return;
    const loaded = new Int16Array((data.byteLength - 44) / 2);
    for (let i = 0; i < loaded.length; i++) loaded[i] = data.getInt16(44 + i * 2, true);
    if (!pcm && !recording && !preparing) { pcm = loaded; preview(blob); info(`Saved message · ${(pcm.length / RATE).toFixed(1)} seconds.`); render(); }
  }).catch(() => {});
  render();
  return {setConnected(value, talking = false) { connected = value; manualTalk = talking; render(); }, stop};
}
