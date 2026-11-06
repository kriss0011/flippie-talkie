// Draws the air: a stacked pulse field of spectra, waveform lanes and the timeline.
// Everything shown comes straight from AirHistory; nothing here invents data.
const FRAME_MS = 33;
const TIMELINE_MS = 180000;

const token = name => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

function surface(canvas) {
  const ratio = Math.min(devicePixelRatio || 1, 2),
    width = canvas.clientWidth,
    height = canvas.clientHeight,
    pixelWidth = Math.round(width * ratio),
    pixelHeight = Math.round(height * ratio);
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  const context = canvas.getContext('2d');
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);
  return {context, width, height};
}

// The noise floor stays low so that speech stands up from the field.
const lift = value => Math.max(0, (value - 90) / 165) ** 1.3;

// Full vermilion means transmitting now. Finished transmissions keep a dimmer record.
function sendingSince(history, now) {
  const last = history.events.at(-1);
  return last?.kind === 'tx' && now - last.end < 500 ? last.start - 500 : Infinity;
}

function drawPulse(canvas, history, now, colors, smooth) {
  const {context, width, height} = surface(canvas);
  if (!width || !height) return;
  const spacing = height < 280 ? 8 : 11,
    rowMs = 150,
    count = Math.ceil(height / spacing) + 1,
    rows = history.ridges(now, rowMs, count),
    shift = smooth ? ((now % rowMs) / rowMs) * spacing : 0,
    bottom = height - 6,
    amplitude = spacing * 6.5,
    step = width / (history.bins - 1),
    firstRow = Math.floor(now / rowMs) - count + 1,
    liveFrom = sendingSince(history, now);
  // Midpoint curves keep the ridges soft without inventing peaks between bins.
  const trace = (row, y, height) => {
    context.moveTo(0, row ? y - lift(row.spectrum[0]) * height : y);
    if (!row) {
      context.lineTo(width, y);
      return;
    }
    let lastX = 0,
      lastY = y - lift(row.spectrum[0]) * height;
    for (let b = 1; b < history.bins; b++) {
      const x = b * step,
        top = y - lift(row.spectrum[b]) * height;
      context.quadraticCurveTo(lastX, lastY, (lastX + x) / 2, (lastY + top) / 2);
      lastX = x;
      lastY = top;
    }
    context.lineTo(lastX, lastY);
  };
  rows.forEach((row, i) => {
    const front = i === rows.length - 1,
      y = bottom - (rows.length - 1 - i) * spacing + shift - (smooth ? spacing : 0);
    if (y < 1 || y > height + 2) return;
    // Rows taper towards the top edge so that no peak is cut off.
    const ridge = Math.min(amplitude, y - 1),
      sentAt = (firstRow + i) * rowMs;
    context.beginPath();
    trace(row, y, ridge);
    context.lineTo(width, y + spacing);
    context.lineTo(0, y + spacing);
    context.closePath();
    context.fillStyle = colors.ground;
    context.fill();
    context.beginPath();
    trace(row, y, ridge);
    context.lineWidth = row && front ? 2 : 1;
    context.strokeStyle = !row
      ? colors.rule
      : row.kind === 'tx'
        ? sentAt >= liveFrom
          ? colors.tx
          : colors.txPast
        : row.gated
          ? colors.dim
          : front
            ? colors.rx
            : colors.ink;
    context.stroke();
  });
}

function drawWave(canvas, history, now, lane, colors) {
  const {context, width, height} = surface(canvas);
  if (!width || !height) return;
  const middle = height / 2,
    columns = history.visible(now, lane),
    step = width / columns.length,
    scale = middle * 2.4,
    groups = new Map(),
    liveFrom = sendingSince(history, now);
  context.fillStyle = colors.rule;
  context.fillRect(0, Math.floor(middle), width, 1);
  columns.forEach((column, i) => {
    if (!column) return;
    const color =
      column.kind === 'tx'
        ? column.index * history.columnMs >= liveFrom
          ? colors.tx
          : colors.txPast
        : column.kind === 'mic'
          ? colors.ink
          : column.gated
            ? colors.dim
            : colors.rx;
    if (!groups.has(color)) groups.set(color, []);
    groups.get(color).push([i * step, column.low, column.high]);
  });
  for (const [color, bars] of groups) {
    context.fillStyle = color;
    for (const [x, low, high] of bars) {
      const top = Math.max(0, middle - high * scale),
        end = Math.min(height, middle - low * scale);
      context.fillRect(x, top, Math.max(1, step), Math.max(1, end - top));
    }
  }
}

function renderTimeline(strip, history, now, onPlay) {
  strip.replaceChildren();
  const from = now - TIMELINE_MS;
  for (const event of history.events) {
    if (event.end < from) continue;
    const left = Math.max(0, (event.start - from) / TIMELINE_MS) * 100,
      width = Math.max(0.5, ((event.end - Math.max(event.start, from)) / TIMELINE_MS) * 100),
      seconds = Math.max(1, Math.round((event.end - event.start) / 1000)),
      time = new Date(event.start).toLocaleTimeString(),
      label = `${event.kind === 'tx' ? 'You talked' : 'Received'} at ${time} for ${seconds} s`,
      block = document.createElement(event.recording ? 'button' : 'span');
    const sending = event.kind === 'tx' && now - event.end < 1500;
    block.className = `air-block ${event.kind}${event.gated ? ' gated' : ''}${sending ? ' live' : ''}`;
    block.style.left = `${left}%`;
    block.style.width = `${Math.min(width, 100 - left)}%`;
    block.title = event.recording ? `${label}. Play recording.` : label;
    if (event.recording) {
      block.setAttribute('aria-label', `Play recording: ${label}`);
      block.onclick = () => onPlay(event.recording);
    }
    strip.append(block);
  }
}

// A live trace of your voice inside the talk button. Null clears it.
export function drawScope(canvas, samples) {
  const {context, width, height} = surface(canvas);
  if (!samples || !width) return;
  const middle = height / 2,
    points = Math.min(160, Math.floor(width / 3)),
    step = Math.floor(samples.length / points);
  context.beginPath();
  for (let i = 0; i < points; i++) {
    const y = middle - Math.max(-1, Math.min(1, samples[i * step] * 4)) * (middle - 1);
    if (i) context.lineTo((i / (points - 1)) * width, y);
    else context.moveTo(0, y);
  }
  context.lineWidth = 1.25;
  context.strokeStyle = '#000';
  context.stroke();
}

export function airDisplay(history, {pulse, received, microphone, strip, onPlay}) {
  const calm = matchMedia('(prefers-reduced-motion: reduce)');
  let colors,
    lastFrame = 0,
    lastStrip = 0,
    idle = false,
    stripKey = '';
  const readColors = () => {
    colors = {
      ground: token('--ground'),
      ink: token('--ink'),
      dim: token('--ink-3'),
      rule: token('--rule'),
      rx: token('--rx'),
      tx: token('--tx'),
      txPast: token('--tx-past'),
    };
  };
  const paint = now => {
    drawPulse(pulse, history, now, colors, !calm.matches);
    drawWave(received, history, now, 'rx', colors);
    drawWave(microphone, history, now, 'mic', colors);
  };
  const frame = time => {
    requestAnimationFrame(frame);
    const gap = calm.matches ? 500 : FRAME_MS;
    if (time - lastFrame < gap) return;
    lastFrame = time;
    const now = Date.now(),
      quiet = now - history.updatedAt > history.spanMs + 1000;
    if (!quiet || !idle) paint(now);
    idle = quiet;
    if (time - lastStrip > 1000) {
      lastStrip = time;
      const key = history.events.map(e => `${e.end}${e.recording}`).join();
      if (key !== stripKey || history.events.length) {
        stripKey = key;
        renderTimeline(strip, history, now, onPlay);
      }
    }
  };
  readColors();
  new ResizeObserver(() => paint(Date.now())).observe(pulse);
  requestAnimationFrame(frame);
}
