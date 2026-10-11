# stm32mp257-platform

STM32MP257F-DK 보드에서 Cortex-A35 Linux와 Cortex-M33 firmware를 함께 올리고, boot chain, kernel driver, M33 firmware 수명주기, A35 ↔ M33 RPMsg 통신을 실제 보드로 검증한 기록입니다.

목표는 이기종 SoC(Cortex-A + Cortex-M)에서 RTOS와 Embedded Linux가 역할을 나누는 구조를 직접 bring-up하고, 각 단계를 로그와 측정값으로 보이는 것입니다. 아래 표의 `검증함`은 보드에서 실행하고 로그를 남긴 항목만 뜻합니다.

## 구조

```text
                 STM32MP257F (MB1605-MP257F-C01, Rev.Y)
 ┌──────────────────────────────────────┐   ┌───────────────────────────────┐
 │ Cortex-A35 x2 (CID1)                 │   │ Cortex-M33 (CID2, non-secure) │
 │  EL3   TF-A BL31 (PSCI, SMC)         │   │  FreeRTOS V10.6.2, 400 MHz    │
 │  S-EL1 OP-TEE + SCMI server          │   │  - LED3 thread (100 ms)       │
 │  EL1   U-Boot → Linux 6.6.129        │   │  - console thread (UART5 RX   │
 │        - remoteproc  ────────────────┼──►│    IRQ → queue → uptime)      │
 │        - mp2_button driver (PC11 IRQ)│   │  - shutdown thread            │
 │        - rpmsg_tty / virtio          │◄─►│    (IPCC shutdown → ack)      │
 └──────────────┬───────────────────────┘   └──────────────┬────────────────┘
                │ IPCC1 (mailbox: vq0, vq1, shutdown)       │
                └───── DDR (M33 code/data, vring, resource table) ──────┘
 RIF: OP-TEE가 부팅 때 peripheral과 GPIO를 CID별로 할당 (예: PH6/LD9 → M33)
```

M33 firmware는 두 가지이고, remoteproc으로 한 번에 하나를 올립니다. 위 그림은 `firmware/m33_freertos`(console, LED)입니다. IPC 시험은 `firmware/m33_openamp`(bare-metal OpenAMP, `/dev/ttyRPMSG0`의 m33_link 서버)로 했습니다. IPC 구성은 `docs/architecture.md` §5에 있습니다.

자세한 내용은 `docs/architecture.md`에 있습니다.

## 결과

| 영역 | 항목 | 상태 | 핵심 근거 |
|---|---|---|---|
| Boot | 출고 이미지 boot chain 분석 (ROM → TF-A BL2 → BL31 → OP-TEE → U-Boot → Linux) | 검증함 | boot log의 image id와 load 주소를 TF-A 소스 정의, 실행 중 DT 예약 영역과 대조 |
| Boot | OpenSTLinux v6.2.1 Starter를 두 번째 microSD로 bring-up | 검증함 (kernel patch 적용) | 기본 kernel은 SD 읽기 데이터가 깨짐. 원인과 수정은 `docs/sd-sdr104-tuning.md` |
| Boot | 단계별 boot time 3회 측정 | 측정함 | TF-A→login 28.0 / 28.0 / 28.2 s, systemd-analyze 21.2–21.5 s |
| Kernel | out-of-tree module build (vermagic 일치), load/unload 10회, 잘못된 parameter 거부 | 검증함 | `modules/hello` |
| Kernel | DT 노드로 probe되는 GPIO interrupt platform driver | 검증함 | `modules/mp2_button`: devm GPIO, threaded IRQ, debounce, sysfs `press_count` |
| Kernel | 실패 경로: DT 노드 없음, GPIO 사용 중(-EBUSY), GPIO 속성 없음(-ENOENT) | 검증함 | probe 실패 후 시스템 유지, unbind/bind로 복구 |
| RTOS | 자체 빌드 FreeRTOS firmware를 remoteproc으로 load/start/stop 3회 | 검증함 | 공장 USB-PD firmware 정지 → 교체 → 복구 |
| RTOS | 잘못된 firmware (없음, 너무 작음, ELF 아님) 거부 | 검증함 | `Boot failed: -2 / -22`, Linux 유지 |
| RTOS | M33 console: HW 정보 출력, UART RX interrupt → uptime, LED 100 ms | 검증함 | tick 간격 5006 ms vs host 5.005 s, LED toggle 50회 / 5 s |
| IPC | RPMsg TTY echo, 채널 2개 | 검증함 | `/dev/ttyRPMSG0`, `/dev/ttyRPMSG1` 각 1000/1000 |
| IPC | 자체 frame 프로토콜 m33_link (magic, version, seq, len, CRC-16; PING/PONG/NACK/GET_STATS) | 검증함 | `ipc/README.md` |
| IPC | round-trip latency, 크기별 1000회 | 측정함 | p50 0 B 80.1, 32 B 105.2, 484 B 483.7 µs (부하 없음, PREEMPT) |
| IPC | 잘못된 frame 6종 거부, 전송 중 M33 정지와 재시작 | 검증함 | NACK 1–6; 정지 중 잘못된 frame 0, Linux 유지, 재시작 후 1000/1000 |
| IPC | GET_STATS로 M33 수신 카운터 조회 | 검증함 | 카운터가 보낸 요청 수와 일치, payload가 붙은 요청은 NACK |
| 설계 | RTOS → Linux 기능 분리 | 기준과 실제 구성 대응, 이후 단계는 설계만 | `docs/architecture.md` §6 |

전체 항목과 측정 방법은 `docs/evidence-index.md`에 있습니다.

## 대표 문제: v6.2.1 SD 읽기 데이터 손상

ST v6.2.1 이미지로 부팅하면 init이 SIGILL로 죽거나, 같은 파일의 md5가 읽을 때마다 달라졌습니다. DDR, CPU 주파수, EL2를 하나씩 배제한 결과, SDR104 모드에서 SDMMC delay-block tuning이 실행되지 않는 것이 원인이었습니다.

- v6.6-stm32mp-r3.1은 STM32MP25의 tuning 최소 주파수를 100 MHz로 올렸습니다.
- 이 보드의 SDR104는 실제 100 MHz로 동작하므로 `actual_clock <= min_freq` 조건에 걸려 tuning을 건너뜁니다.
- 기준을 r1의 50 MHz로 되돌린 패치(`patches/linux/0001-…`)를 적용하자 tuning 로그(`phase:24`)가 나타나고 md5 20/20이 일치했습니다.

과정은 `docs/sd-sdr104-tuning.md`에 정리했습니다.

## 저장소 구성

```text
firmware/m33_freertos/   M33 FreeRTOS app + Makefile (STM32CubeMP2 v1.3.2 기반, ST 파일은 원래 고지 유지)
firmware/m33_openamp/    M33 OpenAMP firmware: ttyRPMSG0 m33_link 서버, ttyRPMSG1 echo (ST OpenAMP_TTY_echo 기반)
ipc/                     m33_link frame 정의(m33_link.h), Linux 시험 도구(linux/m33_link_ping.c), 측정 기록
modules/hello/           최소 kernel module (parameter 범위 검사)
modules/mp2_button/      USER2 버튼 (PC11) platform driver
patches/linux/           v6.2.1 kernel SDMMC tuning 수정
scripts/                 console 파일 전송, VCP 로거, U-Boot DT 노드 주입, boot time 측정
docs/                    구조, 결정, 디버깅 기록, evidence index
```

## 재현

환경: Windows + WSL2 (Ubuntu 22.04), usbipd로 ST-LINK 연결, OpenSTLinux v6.2.1 Developer Package SDK, STM32CubeCLT 1.22.0 (GCC 14.3.1), STM32CubeMP2 v1.3.2.

```sh
# kernel module (SDK environment-setup을 source한 셸)
make -C modules/mp2_button KDIR=<kernel build dir>

# M33 firmware
make -C firmware/m33_freertos CUBE=<STM32CubeMP2 v1.3.2>
make -C firmware/m33_openamp CUBE=<STM32CubeMP2 v1.3.2>

# Linux IPC 시험 도구 (SDK environment-setup을 source한 셸)
make -C ipc/linux

# 보드: M33 firmware 교체. remoteproc 번호는 부팅마다 바뀔 수 있어 name으로 찾습니다.
M=$(grep -l '^m33$' /sys/class/remoteproc/*/name | xargs dirname)
echo stop > $M/state
echo -n /tmp > /sys/module/firmware_class/parameters/path
echo -n m33_freertos_stripped.elf > $M/firmware
echo start > $M/state

# IPC (m33_openamp firmware 실행 중)
./m33_link_ping -n 1000 -s 32     # round trip
./m33_link_ping -x crc            # 오류 frame → NACK
./m33_link_ping -S                # M33 수신 카운터
```

## 범위 밖

- `rpmsg_char`나 전용 kernel rpmsg driver와의 비교, 부하·CPU 고정·PREEMPT_RT 조건의 latency
- M33 crash 감지와 자동 재시작, watchdog
- Yocto 전체 빌드, PREEMPT_RT, NPU (X-LINUX-AI)
- 보안 lifecycle 변경 (OTP, secure boot). Secure boot는 별도 프로젝트(STM32H573 STiRoT → MCUboot)에서 다룹니다.

## 라이선스

저장소 코드는 MIT (`LICENSE`)입니다. `firmware/m33_freertos/`, `firmware/m33_openamp/`의 ST 파일은 원래 고지(`LICENSE-ST.md`)를 따르고, `modules/`는 GPL-2.0, `patches/linux/`는 Linux kernel 라이선스를 따릅니다.
