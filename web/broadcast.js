export function abortableDelay(ms, signal) {
  return new Promise((resolve, reject) => {
    if (signal.aborted) { reject(new Error('Stopped')); return; }
    const abort = () => { clearTimeout(timer); reject(new Error('Stopped')); };
    const timer = setTimeout(() => { signal.removeEventListener('abort', abort); resolve(); }, ms);
    signal.addEventListener('abort', abort, {once: true});
  });
}
export async function repeatMessage({pcm, rate = 6000, count = 1, durationMs = null, gapMs = 5000, signal, send, progress = () => {}, now = () => performance.now(), delay = abortableDelay}) {
  if (!pcm?.length || rate !== 6000 || pcm.length > rate * 45) throw Error('Record a message of up to 45 seconds first.');
  if (!Number.isFinite(gapMs) || gapMs < 1000 || gapMs > 3600000) throw Error('Choose a pause between 1 second and 60 minutes.');
  if (durationMs !== null ? (!Number.isFinite(durationMs) || durationMs < 1000 || durationMs > 3600000) : (!Number.isInteger(count) || count < 1 || count > 1000)) throw Error('Choose 1–1000 sends, or a running time up to 60 minutes.');
  const deadline = durationMs === null ? Infinity : now() + durationMs;
  const messageMs = pcm.length / rate * 1000 + 300;
  if (messageMs > durationMs && durationMs !== null) throw Error('The running time must be longer than the message.');
  let sent = 0;
  while (!signal.aborted && (durationMs !== null || sent < count) && now() + messageMs <= deadline) {
    try { progress({state: 'sending', sent, count, durationMs}); await send(pcm, signal); if (signal.aborted) break; sent++; }
    catch (error) { if (signal.aborted) break; if (error.name !== 'ChannelBusyError') throw error; progress({state: 'busy', sent, count, durationMs}); }
    if ((durationMs === null && sent >= count) || now() + gapMs + messageMs > deadline || signal.aborted) break;
    progress({state: 'waiting', sent, count, durationMs});
    try { await delay(gapMs, signal); } catch (error) { if (!signal.aborted) throw error; }
  }
  return sent;
}
