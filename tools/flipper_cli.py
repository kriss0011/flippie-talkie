import os
import serial
from serial.tools import list_ports
import time

def find_port():
    configured = os.environ.get('FLIPPER_PORT')
    if configured:
        return configured
    candidates = [p.device for p in list_ports.comports()
                  if p.vid == 0x0483 and p.pid == 0x5740]
    if len(candidates) != 1:
        raise RuntimeError('Set FLIPPER_PORT to your Flipper serial port; found: ' + repr(candidates))
    return candidates[0]

class CLI:
    def __init__(self, port=None):
        self.s = serial.Serial(port or find_port(), 115200, timeout=.25, write_timeout=5)
        self.buf = b''
        self.s.write(b'\x03\r')
        self.until(b'>: ', 120)
        self.buf = b''
        self.s.reset_input_buffer()

    def until(self, marker, timeout=15):
        deadline = time.monotonic() + timeout
        while marker not in self.buf:
            if time.monotonic() > deadline:
                raise TimeoutError(f'Waiting for {marker!r}: {self.buf[-150:]!r}')
            self.buf += self.s.read(max(1, self.s.in_waiting))
        result, self.buf = self.buf.split(marker, 1)
        return result

    def exact(self, size, timeout=20):
        deadline = time.monotonic() + timeout
        while len(self.buf) < size:
            if time.monotonic() > deadline:
                raise TimeoutError('Incomplete binary transfer')
            self.buf += self.s.read(max(1, min(size-len(self.buf), 65536)))
        result, self.buf = self.buf[:size], self.buf[size:]
        return result

    def command(self, cmd, timeout=15):
        self.s.write((cmd+'\r').encode())
        self.until(b'\r\n')
        return self.until(b'>: ', timeout).decode('utf-8').strip()

    def read_file(self, path, expected):
        self.s.write(f'storage read_chunks "{path}" 8192\r'.encode())
        self.until(b'\r\n')
        answer = self.until(b'\r\n')
        if not answer.startswith(b'Size: '):
            raise RuntimeError(f'Read failed: {answer!r}')
        size = int(answer.split(b': ')[1])
        if size != expected:
            raise RuntimeError(f'Size changed: {path}')
        result = bytearray()
        while len(result) < size:
            self.until(b'Ready?\r\n')
            self.s.write(b'y')
            result.extend(self.exact(min(8192, size-len(result))))
        self.until(b'>: ')
        return bytes(result)

    def close(self):
        self.s.close()

    def write_new_file(self, path, data):
        status = self.command(f'storage stat "{path}"')
        if not any(message in status for message in ("file/dir not exist", "file/dir doesn't exist")):
            raise RuntimeError(f'Refusing to overwrite or uncertain target: {status}')
        for offset in range(0,len(data),4096):
            chunk=data[offset:offset+4096]
            self.s.write(f'storage write_chunk "{path}" {len(chunk)}\r'.encode())
            self.until(b'\r\n')
            answer=self.until(b'\r\n')
            if b'Storage error:' in answer:
                raise RuntimeError(answer.decode())
            if b'Ready' not in answer:
                raise RuntimeError(f'Unexpected upload handshake: {answer!r}')
            self.s.write(chunk)
            response=self.until(b'>: ',30)
            if b'Storage error:' in response:raise RuntimeError(response.decode())
