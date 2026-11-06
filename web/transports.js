const SERVICE = '8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000';
const uuid = n => `19ed82ae-ed21-4c9d-4145-228e${n}fe0000`;
const sleep = ms => new Promise(r => setTimeout(r, ms));
export class BluetoothLink {
  async open(onData, onClose) {
    if (!navigator.bluetooth)
      throw Error(
        'Use Chrome on Android or a computer, or Bluefy on iPhone and iPad. Safari can’t connect to Bluetooth devices.',
      );
    this.onClose = onClose;
    this.device = await navigator.bluetooth.requestDevice({
      filters: [
        {services: [0x3080]},
        {services: [0x3081]},
        {services: [0x3082]},
        {services: [0x3083]},
        {namePrefix: 'PMR'},
        {namePrefix: 'Flipper'},
      ],
      optionalServices: [SERVICE],
    });
    this.device.addEventListener('gattserverdisconnected', () => {
      this.closed = true;
      onClose();
    });
    try {
      const server = await this.device.gatt.connect(),
        service = await server.getPrimaryService(SERVICE);
      this.rx = await service.getCharacteristic(uuid('62'));
      await this.rx.readValue();
      const tx = await service.getCharacteristic(uuid('61'));
      await tx.startNotifications();
      tx.addEventListener('characteristicvaluechanged', e => {
        const v = e.target.value;
        onData(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
      });
      this.closed = false;
      this.chain = Promise.resolve();
      await sleep(200);
      const flow = await service.getCharacteristic(uuid('63'));
      flow.addEventListener('characteristicvaluechanged', e => {
        this.credit = e.target.value.getUint32(0, false);
      });
      await flow.startNotifications();
      this.credit = (await flow.readValue()).getUint32(0, false);
    } catch (e) {
      this.device.gatt.disconnect();
      throw e;
    }
  }
  write(bytes) {
    const task = this.chain.then(async () => {
      if (this.closed) throw Error('Bluetooth disconnected');
      let offset = 0;
      const deadline = performance.now() + 1200;
      while (offset < bytes.length) {
        while (!this.credit) {
          if (this.closed || performance.now() > deadline)
            throw Error('Bluetooth audio flow timed out. Reconnect your Flipper.');
          await sleep(5);
        }
        const n = Math.min(this.credit, bytes.length - offset);
        this.credit -= n;
        const part = bytes.slice(offset, offset + n);
        await this.rx.writeValueWithoutResponse(part);
        offset += n;
      }
    });
    this.chain = task.catch(() => {});
    return task;
  }
  close() {
    this.closed = true;
    this.device?.gatt.disconnect();
  }
}
export class USBLink {
  async open(onData, onClose) {
    if (!navigator.serial)
      throw Error('USB needs Chrome or Edge on a computer. On a phone, use Bluetooth.');
    this.port = await navigator.serial.requestPort({filters: [{usbVendorId: 0x0483}]});
    await this.port.open({baudRate: 115200});
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.closed = false;
    this.onClose = onClose;
    this.chain = Promise.resolve();
    // The Flipper must already have Flippie Talkie open in USB mode.
    this.reading = (async () => {
      try {
        while (!this.closed) {
          const {value, done} = await this.reader.read();
          if (done) break;
          if (value) onData(value);
        }
      } catch (e) {
        if (!this.closed) this.error = e;
      } finally {
        this.reader.releaseLock();
        if (!this.closed) {
          this.closed = true;
          onClose();
        }
      }
    })();
    await this.write(new TextEncoder().encode('\x03\rpmr_connect usb\r'));
  }
  write(bytes) {
    const task = this.chain.then(() => {
      if (this.closed) throw Error('USB disconnected');
      return this.writer.write(bytes);
    });
    this.chain = task.catch(() => {});
    return task;
  }
  async close() {
    this.closed = true;
    try {
      await this.reader.cancel();
      await this.reading;
      this.writer.releaseLock();
      await this.port.close();
    } catch {}
    this.onClose?.();
  }
}
