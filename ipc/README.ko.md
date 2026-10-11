# m33_link: A35 ↔ M33 framed messages over RPMsg

Linux(Cortex-A35)와 M33(bare-metal OpenAMP) 사이에서 RPMsg TTY 위로 주고받는 요청/응답 형식입니다.

```text
Linux userspace            Linux kernel                    M33 (firmware/m33_openamp)
m33_link_ping  ── write ─► rpmsg_tty ─► virtio vring ─► IPCC ─► OpenAMP ─► VIRT_UART0 callback
                                                                          (copy, set flag)
                                                                main loop: link_srv_handle()
m33_link_ping  ◄─ read ─── rpmsg_tty ◄─ virtio vring ◄─ IPCC ◄─ VIRT_UART_Transmit(PONG/NACK)
```

## Frame

한 RPMsg 메시지에 한 frame을 담습니다. little endian이고 헤더는 12 B입니다(`m33_link.h`).

| offset | 필드 | 크기 | 설명 |
|---|---|---|---|
| 0 | magic | 2 | `0x4C4D` ("ML") |
| 2 | ver | 1 | 1 |
| 3 | type | 1 | 1 PING, 2 PONG, 3 NACK, 4 GET_STATS, 5 STATS |
| 4 | seq | 4 | 요청 번호. 응답은 같은 seq |
| 8 | len | 2 | payload 길이, 최대 484 B (RPMsg buffer 512 − RPMsg header 16 − 12) |
| 10 | crc16 | 2 | CRC-16/CCITT-FALSE, crc16 = 0인 헤더 + payload |

M33의 검사 순서와 NACK 코드:

| 순서 | 검사 | NACK err | NACK의 seq |
|---|---|---|---|
| 1 | 수신 크기 ≥ 12 | 1 SHORT | 0 |
| 2 | magic | 2 MAGIC | 0 |
| 3 | ver | 3 VERSION | 0 |
| 4 | len ≤ 484, 12 + len = 수신 크기 | 4 LEN | 요청 seq |
| 5 | crc16 | 5 CRC | 요청 seq |
| 6 | type = PING 또는 GET_STATS, GET_STATS는 len = 0 | 6 TYPE, len ≠ 0이면 4 LEN | 요청 seq |

GET_STATS는 payload 없이 보내고, M33은 STATS(16 B: rx_ok, rx_bad, last_err, reserved 3 B, tick_ms)로 답합니다. GET_STATS도 rx_ok에 셉니다.

magic과 version이 맞기 전에는 seq를 믿지 않으므로 0을 돌려줍니다. NACK payload에는 오류 코드와 M33의 누적 정상/오류 수신 수가 들어 있습니다.

## 설계 선택

- **TTY 위에 frame을 둔 이유:** ST의 `rpmsg_tty` driver를 그대로 쓰면 kernel 수정 없이 userspace에서 시험할 수 있습니다. 메시지 경계는 RPMsg가 지켜 주므로, frame 안의 len은 경계가 아니라 무결성 검사에 씁니다.
- **CRC를 둔 이유:** vring은 공유 메모리라 전송 오류는 드물지만, 버전이 다른 firmware나 잘못된 writer를 거르는 데 씁니다. 비용은 아래 측정에서 payload 크기에 비례해 보입니다.
- **M33 처리 위치:** RPMsg callback은 복사와 flag 설정만 하고, 검사와 응답은 main loop에서 합니다. callback 안에서 출력하면 UART 시간이 왕복 시간에 더해집니다(ST 예제는 callback에서 수신 내용을 출력함).

## 측정 (2026-10-11, STM32MP257F-DK, Linux 6.6.129, M33 400 MHz)

`m33_link_ping`이 frame을 write하기 직전부터 응답 frame 전체를 read하고 검사할 때까지를 `CLOCK_MONOTONIC`으로 잽니다. 1000회씩, 부하 없음(loadavg 0.10), 요청 하나가 끝나야 다음을 보냅니다.

| payload | ok | min | p50 | p99 | max |
|---|---|---|---|---|---|
| 0 B | 1000/1000 | 70.6 µs | 80.1 µs | 643.0 µs | 1575.4 µs |
| 32 B | 1000/1000 | 102.4 µs | 105.2 µs | 227.2 µs | 915.1 µs |
| 484 B | 1000/1000 | 461.1 µs | 483.7 µs | 1168.3 µs | 2649.2 µs |

- p50은 payload 크기에 따라 늘어납니다(0 → 484 B에서 약 +400 µs). 양쪽의 bit 단위 CRC와 복사가 원인으로 보이지만, 구간별로 나눠 재지는 않았습니다(추정).
- p99와 max의 긴 꼬리는 Linux 쪽 scheduling으로 보입니다. PREEMPT(RT 아님), CPU 고정과 우선순위 설정 없이 잰 값입니다(추정).

## 오류 시험

| 시험 | 결과 |
|---|---|
| SHORT / MAGIC / VERSION / LEN / CRC / TYPE frame 각 1회 | NACK err 1–6, M33 카운터 rx_bad 1→6, rx_ok 3000 유지 |
| 오류 시험 직후 정상 ping 100회 | 100/100 |
| 30000회 송신 중 1 s 후 M33 stop | 정상 8183, 진행 중이던 1회 timeout, 이후 21816회 write 오류 (tty 제거). 잘못된 frame 0, Linux 유지 |
| M33 offline 상태에서 실행 | `open /dev/ttyRPMSG0: No such file or directory` |
| M33 start 후 1000회 | 1000/1000, p50 105.7 µs |

## Build

```sh
# M33
make -C firmware/m33_openamp
# Linux tool: source the OpenSTLinux SDK environment-setup first
make -C ipc/linux
```
