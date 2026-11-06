import {RecordingStore} from './recording-store.js';
const $ = id => document.getElementById(id);
const duration = seconds =>
  `${Math.floor(seconds / 60)}:${String(Math.floor(seconds % 60)).padStart(2, '0')}`;
export function recordingLibrary(onToggle) {
  const store = new RecordingStore(),
    rows = new Map(),
    clips = new Map(),
    temporary = new Map();
  let playingURL,
    message = 'Auto-record is off.',
    saving = 0;
  const status = () => {
    $('recording-status').textContent = saving ? 'Saving recording…' : message;
  };
  const summary = () => {
    const bytes = [...clips.values()].reduce((n, c) => n + c.bytes, 0);
    $('recording-count').textContent = `${clips.size} saved · ${(bytes / 1048576).toFixed(1)} MB`;
    $('recordings-empty').hidden = clips.size > 0;
    $('recording-summary').hidden = clips.size === 0;
  };
  async function blob(clip) {
    const result = temporary.get(clip.id) || (await store.audio(clip.id));
    if (!result) throw new Error('This recording is no longer in browser storage.');
    return result;
  }
  function showError(error) {
    message = error.message;
    status();
  }
  function add(clip) {
    clips.set(clip.id, clip);
    if (rows.has(clip.id)) return;
    const row = document.createElement('li');
    row.className = 'recording-item';
    const info = document.createElement('div'),
      title = document.createElement('strong'),
      detail = document.createElement('span');
    title.textContent = `Channel ${clip.channel} · ${duration(clip.duration)}`;
    detail.textContent =
      new Date(clip.startedAt).toLocaleString() +
      (clip.temporary ? ' · This session only' : '') +
      (clip.missingSamples
        ? ` · ${(clip.missingSamples / clip.rate).toFixed(1)}s missing audio`
        : '') +
      (clip.reason !== 'Transmission ended' ? ` · ${clip.reason}` : '');
    info.append(title, detail);
    const actions = document.createElement('div');
    actions.className = 'recording-actions';
    const play = document.createElement('button');
    play.textContent = 'Play';
    play.className = 'secondary';
    play.onclick = async () => {
      try {
        const source = await blob(clip);
        const player = $('recording-player');
        player.pause();
        if (playingURL) URL.revokeObjectURL(playingURL);
        playingURL = URL.createObjectURL(source);
        player.src = playingURL;
        player.dataset.clip = clip.id;
        player.hidden = false;
        $('playing-label').textContent =
          `Channel ${clip.channel} · ${new Date(clip.startedAt).toLocaleTimeString()}`;
        await player.play();
      } catch (error) {
        showError(error);
      }
    };
    const download = document.createElement('button');
    download.textContent = 'Download';
    download.className = 'secondary';
    download.onclick = async () => {
      try {
        const url = URL.createObjectURL(await blob(clip)),
          a = document.createElement('a');
        a.href = url;
        a.download = `Flippie-Talkie-CH${String(clip.channel).padStart(2, '0')}-${new Date(clip.startedAt).toISOString().replace(/[:.]/g, '-')}.wav`;
        a.textContent = 'Download recording';
        document.body.append(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(url), 60000);
      } catch (error) {
        showError(error);
      }
    };
    const remove = document.createElement('button');
    remove.textContent = 'Delete';
    remove.className = 'text-button';
    remove.onclick = async () => {
      if (
        !confirm(
          'Delete this recording from this browser? This can’t be undone, so download it first if you want a copy.',
        )
      )
        return;
      try {
        if (!temporary.has(clip.id)) await store.remove(clip.id);
        temporary.delete(clip.id);
        clips.delete(clip.id);
        rows.delete(clip.id);
        row.remove();
        if ($('recording-player').dataset.clip === clip.id) {
          $('recording-player').pause();
          $('recording-player').removeAttribute('src');
          $('recording-player').load();
          $('recording-player').hidden = true;
          URL.revokeObjectURL(playingURL);
          playingURL = null;
          $('playing-label').textContent = 'Choose a recording to play.';
        }
        summary();
      } catch (error) {
        showError(error);
      }
    };
    actions.append(play, download, remove);
    row.append(info, actions);
    rows.set(clip.id, row);
    const next = [...$('recordings-list').children].find(
      child => Number(child.dataset.started) < clip.startedAt,
    );
    row.dataset.started = clip.startedAt;
    $('recordings-list').insertBefore(row, next || null);
    summary();
  }
  $('auto-record').onchange = () => {
    const enabled = $('auto-record').checked;
    onToggle(enabled);
    message = enabled ? 'Waiting for a transmission…' : 'Auto-record is off.';
    status();
    if (enabled) navigator.storage?.persist?.().catch(() => {});
  };
  const loaded = store
    .list()
    .then(items => {
      items.filter(clip => clip.kind !== 'message').forEach(add);
      summary();
    })
    .catch(() => {
      message =
        'Browser storage is unavailable. Recordings last for this session only, so download them before you leave.';
      status();
    });
  return {
    loaded,
    update(active) {
      if (active)
        $('recording-status').textContent =
          `Recording channel ${active.channel} · ${duration(active.samples / active.rate)}`;
      else status();
    },
    async save(recording) {
      const clip = {...recording, id: crypto.randomUUID(), bytes: recording.blob.size};
      saving++;
      status();
      try {
        await store.save(clip);
        message = 'Recording saved in this browser.';
      } catch {
        temporary.set(clip.id, clip.blob);
        clip.temporary = true;
        $('auto-record').checked = false;
        onToggle(false);
        message =
          'Browser storage is full or blocked, so auto-record stopped. Download this recording now.';
      } finally {
        const {blob: _, ...metadata} = clip;
        add(metadata);
        saving--;
        status();
      }
    },
  };
}
