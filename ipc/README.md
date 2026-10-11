# m33_link, framed messages between the A35 and the M33 over RPMsg

m33_link is the request and response format that Linux on the Cortex-A35 and the bare-metal OpenAMP firmware on the M33 exchange over an RPMsg TTY.

```text
Linux userspace            Linux kernel                    M33 (firmware/m33_openamp)
m33_link_ping  ── write ─► rpmsg_tty ─► virtio vring ─► IPCC ─► OpenAMP ─► VIRT_UART0 callback
                                                                          (copy, set flag)
                                                                main loop: link_srv_handle()
m33_link_ping  ◄─ read ─── rpmsg_tty ◄─ virtio vring ◄─ IPCC ◄─ VIRT_UART_Transmit(PONG/NACK)
```

## Frame

Each RPMsg message carries one frame. Fields are little endian, and the header is 12 B (`m33_link.h`).

| Offset | Field | Size | Meaning |
|---|---|---|---|
| 0 | magic | 2 | `0x4C4D` ("ML") |
| 2 | ver | 1 | 1 |
| 3 | type | 1 | 1 PING, 2 PONG, 3 NACK, 4 GET_STATS, 5 STATS |
| 4 | seq | 4 | Request number. A reply carries the same seq |
| 8 | len | 2 | Payload length, at most 484 B (RPMsg buffer 512 minus the 16 B RPMsg header minus 12) |
| 10 | crc16 | 2 | CRC-16/CCITT-FALSE over the header with crc16 set to 0, then the payload |

The M33 checks a frame in this order and sends a NACK with the code of the first check that fails.

| Order | Check | NACK err | seq in the NACK |
|---|---|---|---|
| 1 | Received size ≥ 12 | 1 SHORT | 0 |
| 2 | magic | 2 MAGIC | 0 |
| 3 | ver | 3 VERSION | 0 |
| 4 | len ≤ 484 and 12 + len equals the received size | 4 LEN | Request seq |
| 5 | crc16 | 5 CRC | Request seq |
| 6 | type is PING or GET_STATS, and GET_STATS has len 0 | 6 TYPE, or 4 LEN when a GET_STATS has a payload | Request seq |

A GET_STATS has no payload. The M33 answers with a STATS frame whose 16 B payload holds rx_ok, rx_bad, last_err, 3 reserved bytes, and tick_ms. The GET_STATS itself counts in rx_ok.

The M33 does not trust seq until magic and version match, so it returns 0 for those errors. A NACK payload carries the error code and the M33's running counts of accepted and rejected frames.

## Design choices

- The frame rides on a TTY because ST's `rpmsg_tty` driver then works unchanged, and userspace can test the protocol with no kernel change. RPMsg keeps message boundaries, so len inside the frame checks integrity rather than marking a boundary.
- The CRC is there although the vring is shared memory and bit errors are unlikely. It catches firmware of another protocol version and a writer that ignores the format. Its cost grows with the payload size in the measurements below.
- The RPMsg callback only copies the data and sets a flag. The main loop checks the frame and replies. A print inside the callback would add the UART time to the round trip, and the ST example prints every message there.

## Measurements (2026-10-11, STM32MP257F-DK, Linux 6.6.129, M33 at 400 MHz)

`m33_link_ping` reads `CLOCK_MONOTONIC` just before it writes a frame and again after it has read and checked the whole reply. Each size ran 1000 times with no load (loadavg 0.10). The tool sends the next request only after the previous reply arrives.

| Payload | OK | min | p50 | p99 | max |
|---|---|---|---|---|---|
| 0 B | 1000/1000 | 70.6 µs | 80.1 µs | 643.0 µs | 1575.4 µs |
| 32 B | 1000/1000 | 102.4 µs | 105.2 µs | 227.2 µs | 915.1 µs |
| 484 B | 1000/1000 | 461.1 µs | 483.7 µs | 1168.3 µs | 2649.2 µs |

- p50 grows with the payload, by about 400 µs from 0 B to 484 B. The bit-by-bit CRC and the copies on both sides are the likely cause. This is an inference, because the time was not split by stage.
- The long p99 and max tails most likely come from Linux scheduling. This is also an inference. The kernel was PREEMPT, not RT, and the tool ran with no CPU pinning or priority setting.

## Error tests

| Test | Result |
|---|---|
| One SHORT, MAGIC, VERSION, LEN, CRC, and TYPE frame each | NACK err 1–6. The M33 counter rx_bad went from 1 to 6 while rx_ok stayed at 3000 |
| 100 normal pings right after the error tests | 100/100 |
| M33 stopped 1 s into 30000 sends | 8183 OK and 1 timeout for the request in flight. The next 21816 writes failed because the tty was gone. 0 malformed frames, and Linux stayed up |
| Tool run while the M33 is offline | `open /dev/ttyRPMSG0: No such file or directory` |
| 1000 pings after the M33 starts again | 1000/1000, p50 105.7 µs |

## Build

```sh
# M33
make -C firmware/m33_openamp
# Linux tool: source the OpenSTLinux SDK environment-setup first
make -C ipc/linux
```
