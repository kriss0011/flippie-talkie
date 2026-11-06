export const TX_LEAD_IN_MS = 500;
// The Flipper ends any transmission after this long.
export const TX_LIMIT_MS = 60000;
// Counts up while talking, then down over the final ten seconds.
export function talkClock(elapsedMs) {
  const left = Math.ceil((TX_LIMIT_MS - elapsedMs) / 1000);
  if (left <= 10) return `${Math.max(left, 0)} s left`;
  const seconds = Math.floor(elapsedMs / 1000);
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}
// Stop is acknowledged by the radio after its queued speech and lead-in drain.
export async function waitForTxIdle({
  epoch,
  getStatus,
  isReady,
  signal,
  now = () => performance.now(),
  delay = ms => new Promise(resolve => setTimeout(resolve, ms)),
  timeoutMs = 2500,
}) {
  const deadline = now() + timeoutMs;
  while (now() < deadline) {
    if (signal?.aborted) throw Error('Transmission stopped.');
    if (!isReady()) throw Error('Flipper disconnected before the message finished.');
    const status = getStatus();
    if (status?.epoch === epoch && status.state < 3) {
      if (status.error) throw Error('The radio stopped before the message finished.');
      return;
    }
    await delay(10);
  }
  throw Error('The radio did not confirm the end of the message.');
}
