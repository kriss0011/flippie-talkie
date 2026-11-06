"""Record the Flipper's screen as video while pressing buttons from a script.

Streams real frames over USB RPC and writes one MP4 per clip. Only short presses are
sent; nothing here holds OK or selects a transmit row, so no clip transmits.

    python tools/flipper_record.py local/flipper-clips menu listen
"""
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from flipper_cli import CLI

KEYS = {'up': 0, 'down': 1, 'right': 2, 'left': 3, 'ok': 4, 'back': 5}
PRESS, RELEASE, SHORT = 0, 1, 2
FPS = 30
SCALE = 15  # 128 x 64 becomes 1920 x 960
INK = bytes((25, 20, 10))
GLOW = bytes((240, 175, 30))


def varint(value):
    out = bytearray()
    while value > 127:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def read_varint(data, offset=0):
    value = shift = 0
    while True:
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
        shift += 7


def fields(data):
    offset = 0
    while offset < len(data):
        key, offset = read_varint(data, offset)
        if key & 7 == 0:
            value, offset = read_varint(data, offset)
        elif key & 7 == 2:
            size, offset = read_varint(data, offset)
            value = data[offset:offset + size]
            offset += size
        else:
            raise ValueError(key & 7)
        yield key >> 3, value


class Session:
    """One RPC session: a reader thread collects frames while the script presses keys."""

    def __init__(self):
        self.cli = CLI()
        self.cli.s.write(b'start_rpc_session\r')
        self.cli.until(b'\r\n')
        self.command = 0
        self.lock = threading.Lock()
        self.frames = []
        self.stopping = False
        self.stopped = threading.Event()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        self.request(20)

    def request(self, field, body=b''):
        self.command += 1
        payload = b'\x08' + varint(self.command) + varint((field << 3) | 2) + varint(len(body)) + body
        with self.lock:
            self.cli.s.write(varint(len(payload)) + payload)

    def read(self):
        while not self.stopped.is_set():
            size = shift = 0
            while True:
                byte = self.cli.exact(1, 600)[0]
                size |= (byte & 127) << shift
                if byte < 128:
                    break
                shift += 7
            message = dict(fields(self.cli.exact(size, 60)))
            if 22 in message:
                frame = dict(fields(message[22])).get(1)
                if frame and len(frame) == 1024:
                    self.frames.append((time.monotonic(), frame))
            if self.stopping and message.get(1) == self.stop_command:
                self.stopped.set()

    def key(self, name, hold=0.09):
        code = b'\x08' + varint(KEYS[name])
        for kind in (PRESS, SHORT, RELEASE):
            self.request(23, code + b'\x10' + varint(kind))
            time.sleep(hold if kind == PRESS else 0.02)

    def close(self):
        self.request(21)
        self.stopping = True
        self.request(19)
        self.stop_command = self.command
        self.stopped.wait(5)
        self.cli.s.write(b'\r')
        time.sleep(0.3)
        self.cli.close()


def header(frame):
    """The title bar as bits, for checking which screen is showing."""
    return bytes(frame[x + (y // 8) * 128] >> (y % 8) & 1 for y in range(12) for x in range(128))


def reference(path):
    """The same bits from a saved screenshot in docs/images."""
    from PIL import Image

    image = Image.open(path).convert('L').resize((128, 64), Image.NEAREST)
    return bytes(image.getpixel((x, y)) < 100 for y in range(12) for x in range(128))


class WrongScreen(RuntimeError):
    pass


def expect(session, wanted, name=''):
    """Stops the script unless the live title bar matches. Nothing is pressed blind."""
    time.sleep(0.35)
    live = header(session.frames[-1][1])
    differing = sum(a != b for a, b in zip(live, wanted))
    if differing > 12:
        raise WrongScreen(f'Expected {name}: title bar differs by {differing} pixels')


def selected_row(frame):
    """The highlighted row of a menu, or None."""
    rows = [row for row in range(4) if frame[2 + ((21 + row * 12) // 8) * 128] >> ((21 + row * 12) % 8) & 1]
    return rows[0] if len(rows) == 1 else None


# Letter centres on the on-screen keyboard: rows at the same heights, x in pixels.
KEYBOARD = {
    'Q': (0, 4), 'W': (0, 13), 'E': (0, 22), 'R': (0, 32), 'T': (0, 40), 'Y': (0, 49), 'U': (0, 58),
    'I': (0, 67), 'O': (0, 77), 'P': (0, 85),
    'A': (1, 4), 'S': (1, 13), 'D': (1, 22), 'F': (1, 32), 'G': (1, 40), 'H': (1, 49), 'J': (1, 58),
    'K': (1, 67), 'L': (1, 77),
    '_': (2, 74), 'SAVE': (2, 88),
}
KEY_BANDS = ((28, 38), (40, 50), (52, 62))


def highlighted_key(frame):
    """Finds the inverted key on the keyboard as (row, x): the densest dark patch."""
    pixel = lambda x, y: frame[x + (y // 8) * 128] >> (y % 8) & 1
    best = None
    for row, (top, bottom) in enumerate(KEY_BANDS):
        columns = [sum(pixel(x, y) for y in range(top, bottom)) for x in range(128)]
        for x in range(120):
            dark = sum(columns[x:x + 8])
            if dark >= 48 and (best is None or dark > best[0]):
                best = (dark, row, x + 4)
    return best[1:] if best else None


def type_key(session, letter, pause=0.14):
    """Steers to a key by watching the screen, then presses it."""
    row, x = KEYBOARD[letter]
    for _ in range(40):
        time.sleep(pause)
        at = highlighted_key(session.frames[-1][1])
        if at is None:
            raise WrongScreen('No highlighted key found')
        if at[0] != row:
            session.key('down' if at[0] < row else 'up', hold=0.05)
        elif abs(at[1] - x) > (11 if letter == 'SAVE' else 4):
            session.key('right' if at[1] < x else 'left', hold=0.05)
        else:
            session.key('ok', hold=0.05)
            time.sleep(0.3)
            return
    raise WrongScreen(f'Could not reach {letter}')


def rgb(frame):
    out = bytearray(128 * 64 * 3)
    for y in range(64):
        row = (y // 8) * 128
        bit = 1 << (y % 8)
        for x in range(128):
            out[(y * 128 + x) * 3:(y * 128 + x) * 3 + 3] = INK if frame[row + x] & bit else GLOW
    return bytes(out)


def encode(frames, start, end, path):
    ffmpeg = subprocess.Popen(
        ['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
         '-s', '128x64', '-r', str(FPS), '-i', '-',
         '-vf', f'scale={128 * SCALE}:{64 * SCALE}:flags=neighbor',
         '-c:v', 'libx264', '-crf', '12', '-preset', 'slow', '-pix_fmt', 'yuv420p', str(path)],
        stdin=subprocess.PIPE)
    cache, index, current = {}, 0, frames[0][1]
    for tick in range(int((end - start) * FPS)):
        now = start + tick / FPS
        while index < len(frames) and frames[index][0] <= now:
            current = frames[index][1]
            index += 1
        if current not in cache:
            cache[current] = rgb(current)
        ffmpeg.stdin.write(cache[current])
    ffmpeg.stdin.close()
    ffmpeg.wait()


def record(session, name, steps, out):
    """Steps are key names, seconds to wait, or (title bar, name) checks."""
    first = len(session.frames)
    start = time.monotonic()
    for step in steps:
        if isinstance(step, str):
            session.key(step)
        elif isinstance(step, tuple):
            expect(session, *step)
        elif callable(step):
            step(session)
        else:
            time.sleep(step)
    end = time.monotonic()
    frames = session.frames[first:] or session.frames[-1:]
    if first and session.frames[first - 1:first]:
        frames = [session.frames[first - 1]] + frames
    encode(frames, start, end, out / f'{name}.mp4')
    print(f'{name}: {end - start:.1f} s, {len(frames)} frames')
