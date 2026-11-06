// Standard play, seek and time controls in place of the browser's own audio bar.
const clock = seconds => {
  const whole = Number.isFinite(seconds) ? Math.floor(seconds) : 0;
  return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, '0')}`;
};

export function audioBar(audio, label) {
  const bar = document.createElement('div'),
    toggle = document.createElement('button'),
    seek = document.createElement('input'),
    time = document.createElement('output');
  bar.className = 'audio-bar';
  toggle.className = 'secondary compact';
  toggle.type = 'button';
  seek.type = 'range';
  seek.min = 0;
  seek.max = 1000;
  seek.value = 0;
  seek.setAttribute('aria-label', `${label} position`);
  const show = () => {
    const position = audio.duration ? audio.currentTime / audio.duration : 0;
    seek.value = Math.round(position * 1000);
    seek.style.setProperty('--fill', `${position * 100}%`);
    time.textContent = `${clock(audio.currentTime)} / ${clock(audio.duration)}`;
    toggle.textContent = audio.paused ? 'Play' : 'Pause';
    toggle.setAttribute('aria-label', `${audio.paused ? 'Play' : 'Pause'} ${label.toLowerCase()}`);
  };
  toggle.onclick = () => (audio.paused ? audio.play().catch(() => {}) : audio.pause());
  seek.oninput = () => {
    if (audio.duration) audio.currentTime = (seek.value / 1000) * audio.duration;
  };
  for (const name of ['timeupdate', 'durationchange', 'play', 'pause', 'ended', 'emptied'])
    audio.addEventListener(name, show);
  audio.controls = false;
  bar.append(toggle, seek, time);
  audio.after(bar);
  show();
}
