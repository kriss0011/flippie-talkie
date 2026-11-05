# Flippie Talkie wire protocol, version 3

The implementation is in `firmware/pmr_connect/pmr_connect.c`, `pmr_codec.h`, and `web/{transports,codec,app}.js`. This protocol is for the custom Flippie Talkie app, not the official Flipper RPC protocol.

The app is now Flippie Talkie 0.4; protocol version remains 3. Existing CLI commands, frame markers and the USB handshake remain compatible.

## Framing

All multibyte frame fields are little-endian. A frame has an 8-byte header followed by 0–164 payload bytes:

| Byte | Meaning |
| --- | --- |
| 0–1 | ASCII `PR` |
| 2 | Message type |
| 3 | PTT epoch; use a new epoch for each transmit session |
| 4–5 | Payload length, unsigned 16-bit |
| 6–7 | Sequence number, unsigned 16-bit |
| 8… | Payload |

Frames may arrive fragmented or several at once. Parsers resynchronize on `PR` and reject oversized payloads. There is no additional frame CRC. Sequence counters are diagnostic, not a retransmission mechanism. Audio blocks are independently decodable.

## Browser → Flipper

| Type | Payload | Purpose |
| --- | --- | --- |
| `0x01` | Empty | Heartbeat |
| `0x02` | One byte, 0/1 | Pause/enable listening |
| `0x03` | One byte, 1–16 | Select channel; rejected during pending/active PTT |
| `0x04` | One byte, current channel | Start PTT with header epoch; can be rejected if channel is busy |
| `0x05` | 164-byte IMA block | Microphone audio for the current epoch |
| `0x06` | Empty or byte 0; byte 1 for graceful stop | Stop current epoch immediately, or allow up to 180 ms drain |
| `0x07` | Empty | Stop listening/transmitting; terminate USB binary session |

Type 20 is an internal transport-switch command used by the Flipper CLI/UI; normal web clients do not need it. The web client disconnects its BLE connection after sending its disconnect command.

## Flipper → browser

| Type | Payload |
| --- | --- |
| `0x81` | 26-byte status |
| `0x82` | 164-byte received-audio IMA block |
| `0x83` | 164-byte diagnostic load packet; not audio |

Status is prioritized over queued audio and sent on changes plus periodically (two seconds during BLE reception, one second otherwise). Clients must count incoming audio as link activity too; sparse status alone must not disconnect a healthy stream.

### Status layout

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u8 | Channel |
| 1 | u8 | Transport: 0 BLE, 1 USB (2 is a temporary mode-switch state) |
| 2 | u8 | State: 0 idle, 1 listening, 2 receiving, 3 buffering PTT, 4 transmitting |
| 3 | u8 | Listening enabled |
| 4 | i8 | RSSI in dBm |
| 5 | u8 | PTT epoch |
| 6 | u8 | Error/status code |
| 7 | u8 | Protocol version, 3 |
| 8 | u16 | Buffered transmit samples |
| 10 | u16 | Transmit sample rate, 6000 Hz |
| 12 | u32 | Dropped receive packets |
| 16 | u32 | Transmit underrun samples |
| 20 | u32 | Dropped transmit blocks |
| 24 | u16 | Receive sample rate: 3200 Hz BLE, 4000 Hz USB |

Relevant errors: 1 busy channel, 2 transmit unavailable/blocked, 3 channel change while talking, 4 receive start failure, 5 timeout/expired session, 8 invalid data, 9 buffer/queue overflow. See code for precise state transitions.

## Audio and timing

Each IMA ADPCM block is 164 bytes for 321 signed 16-bit mono samples: initial predictor `i16`, step index `u8` (0–88), reserved zero byte, then 160 bytes containing low-nibble-first codes. The block predictor is its first output sample. A malformed index, reserved byte or length is rejected. Tests verify C/JavaScript agreement and independent FFmpeg decoding.

Host microphone input is resampled continuously to 6000 Hz. Transmit blocks therefore span 53.5 ms. Two blocks are buffered before RF starts. The radio output expands samples to nominal 48 kHz one-bit PDM using asynchronous FSK transmission. Received RSSI is sampled at 8 kHz, filtered, then resampled to 3.2 kHz for BLE or 4 kHz for USB; received blocks span 100.3125 ms or 80.25 ms respectively. Decode using the rates reported in status, not one shared rate for both directions.

During PTT, keep audio flowing; heartbeat alone cannot keep RF keyed. Missing audio for over 400 ms, lost client contact for over 1500 ms, disconnection, or 60 seconds of active RF causes stop. These are software limits, with scheduling/transport latency; the measured missing-audio stop was about 0.51 seconds after the host's final write. Explicit release sends a graceful STOP; emergency/disconnect paths request an immediate stop. Use the matching epoch for audio and STOP so delayed packets from an earlier PTT do not control a new one.

## Bluetooth GATT

The app reuses the Xtreme firmware's serial GATT profile:

| Role | UUID |
| --- | --- |
| Service | `8fe5b3d5-2e7f-4a98-2a48-7acc60fe0000` |
| Host writes / secure initial read | `19ed82ae-ed21-4c9d-4145-228e62fe0000` |
| Device sends indications | `19ed82ae-ed21-4c9d-4145-228e61fe0000` |
| Flow control | `19ed82ae-ed21-4c9d-4145-228e63fe0000` |

Read the secure write characteristic first to initiate authentication, then subscribe to the device indications and flow-control notifications. The flow-control value is an unsigned 32-bit **big-endian** credit count (unlike frame integers), initially up to 4096 bytes. Refill notifications replace available credit; decrement it as bytes are written. A write may split a frame at a credit boundary.

Use `writeValueWithoutResponse` for streaming audio. Writes with response were too slow on the tested link. The tested audio frame is 172 bytes including header; phone BLE/MTU behavior still needs testing. The app waits for the authenticated Bluetooth state before taking over the serial service from RPC. Standard mobile RPC clients cannot use that connection concurrently.

## USB serial

Open Flippie Talkie in USB mode before connecting. Open the serial port at 115200 baud and send:

```text
<Ctrl-C><CR>pmr_connect usb<CR>
```

The CLI responds `PMRCONNECT1\r\n`, after which the same binary framing applies. The textual handshake string is retained for compatibility even though status reports protocol 3. Disconnect command 7 stops the session and returns `CONNECT_END` and the CLI prompt. Ignore CLI text while resynchronizing the binary parser.

Diagnostic CLI commands, while the app is running and no binary session owns the port:

```text
pmr_connect
pmr_connect mode bluetooth
pmr_connect mode usb
pmr_connect linktest
```

`linktest` schedules eight seconds of synthetic 164-byte `0x83` packets at approximately the selected transport’s receive-audio rate. It does not prove RF audio quality. The v0.3 result is stored in `validation/connect-v0.3/ble-capacity.json`.

Version 0.3 adds no frame layout changes. The new browser accepts protocol 2 / 4 kHz for backward compatibility and protocol 3 / the advertised 3.2 or 4 kHz rate. Older browser code rejects the new rate/version; refresh the page after updating the FAP. The browser now renders through a continuous AudioWorklet queue, with a 240 ms target and small clock-drift correction. Short lost intervals fade to silence rather than fabricating speech.
