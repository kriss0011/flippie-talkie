export const TX_LEAD_IN_MS = 500;
// Stop is acknowledged by the radio after its queued speech and lead-in drain.
export async function waitForTxIdle({epoch, getStatus, isReady, signal, now = () => performance.now(), delay = ms => new Promise(resolve => setTimeout(resolve, ms)), timeoutMs = 2500}) {
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
