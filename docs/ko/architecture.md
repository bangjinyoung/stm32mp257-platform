# Architecture

STM32MP257F-DK에서 확인한 구조입니다. 각 절에서 측정한 사실과 설계 가정을 구분해 적습니다.

## 1. Boot chain

| 단계 | 실행 위치 | 역할 | 확인 근거 |
|---|---|---|---|
| ROM | SoC 내장 | BOOT 핀(SD)으로 SDMMC1의 fsbl1 선택 | `Using SDMMC / Instance 1 / Boot used partition fsbl1` |
| TF-A BL2 | 내부 SRAM, EL3 | DDR firmware load, LPDDR4 초기화, FIP에서 아래 image load | image id와 load 주소 (`DDR_FW_ID` 0x0e041000, `BL31` 0x0e000000, `BL32_EXTRA1` 0x82000000, `BL33` 0x84000000) |
| TF-A BL31 | EL3 | PSCI, SMC monitor | `Entry point 0xe000000, SPSR 0x3cd` |
| OP-TEE | DDR 0x82000000, S-EL1 | secure OS + SCP-firmware (SCMI 서버: clock, regulator, reset) | `OP-TEE ST profile: secure_and_system_services`, `SCP-firmware 2.x` |
| U-Boot | DDR 0x84000000, EL1(v6.0) / EL2(v6.2.1) | extlinux로 kernel, initrd, DTB load | `SPSR 0x3c5`, extlinux 로그 |
| Linux | 6.6.x, SMP PREEMPT | remoteproc으로 M33 firmware 시작 | `remoteproc … Booting fw image …` |

- 출고 이미지는 개발용 보안 설정입니다(`All debug access are allowed`). 양산 보안 구성이 아닙니다.
- Boot time (v6.2.1, soft reboot 3회, host 시계): TF-A→U-Boot 1.77 s, U-Boot→kernel 6.1 s (autoboot 1 s와 extlinux 메뉴 대기 포함), kernel→login 20.1–20.3 s.

## 2. A35와 M33의 자원 분할

두 코어는 같은 SoC 버스와 DDR을 쓰지만, 어떤 peripheral과 메모리를 누가 쓸지는 하드웨어가 강제합니다.

- **RIF (Resource Isolation Framework)**: 버스 접근마다 compartment ID(CID)를 붙입니다. A35는 CID1, M33은 CID2입니다. RIFSC(peripheral), RISAB/RISAF(메모리), GPIO 핀마다 허용 CID를 설정합니다. 설정이 맞지 않는 CID의 접근은 차단됩니다(RM0457 §6.2).
- **할당 시점**: 부팅 때 OP-TEE가 RIF 설정 DT(`stm32mp257f-dk-ca35tdcid-ostl-rif.dtsi`)로 할당하고 잠급니다. 예를 들어 PH6(LD9)는 CID2 non-secure로, PH7(Linux heartbeat LED1)는 CID1로 할당되어 있습니다.
- **Linux 쪽 표시**: Linux DT의 각 peripheral 노드에는 `access-controllers = <&rifsc N>`가 붙어 있습니다. M33이 쓰는 UART5(`serial@40110000`)는 Linux DT에서 `status = "disabled"`입니다.
- **M33 쪽 요청**: M33 BSP는 UART5를 쓰기 전에 resource manager(`ResMgr_Request(RIFSC, UART5)`)로 접근 권한을 확인합니다.
- **공유 자원**: DDR의 일부 영역(M33 code/data, vring, resource table)은 Linux DT의 `reserved-memory`로 Linux가 쓰지 않도록 막습니다. IPCC(mailbox)는 두 코어가 각자의 채널 방향으로 함께 씁니다.

정리하면, 같은 peripheral을 두 코어가 동시에 쓰는 구조는 기본이 아닙니다. 하나의 소유자를 정하고, 다른 코어는 IPC로 요청합니다. 공장 firmware의 USB-PD가 예입니다. M33이 UCPD를 소유하고, Linux는 RPMsg 위의 가상 I2C(`rpmsg_i2c`)로 접근합니다.

## 3. M33 수명주기 (remoteproc)

| 동작 | 경로 | 보드 확인 |
|---|---|---|
| load / start | Linux `remoteproc`가 ELF를 DDR에 올리고 M33 reset 해제 | 3회 정상, 잘못된 ELF는 `-2`/`-22`로 거부 |
| stop | Linux가 IPCC `shutdown` 채널로 요청 → M33 shutdown thread가 정리 후 ack → Linux가 reset | M33 로그 `[m33] shutdown tick=… led_toggles=…` 후 `stopped remote processor m33` |
| 복구 | 기본 firmware로 다시 start | `rpmsg_i2c`, `/sys/class/typec/port0` 복구 |

- M33 firmware는 `IS_DEVELOPER_BOOT_MODE()`로 실행 모드를 구분합니다. remoteproc으로 실행되면 클럭은 A35 쪽이 이미 설정했으므로 `SystemCoreClockUpdate()`만 하고, IPCC와 CoproSync를 초기화합니다.
- remoteproc 번호는 부팅마다 바뀔 수 있습니다. 한 부팅에서는 M33이 `remoteproc1`, 다른 부팅에서는 `remoteproc0`이었습니다. 스크립트는 `name`으로 찾아야 합니다.

## 4. M33 firmware 구조 (`firmware/m33_freertos`)

```text
UART5 RX IRQ ─► HAL callback ─► osMessageQueuePut ─► console thread ─► printf (UART5 TX)
TIM6 1 kHz ─► HAL tick / FreeRTOS tick
LED thread: osDelayUntil(100 ms) ─► BSP_LED_Toggle(LED3 = PH6)
IPCC shutdown IRQ ─► semaphore ─► shutdown thread ─► UART5 IRQ off, threads 종료 ─► IPCC ack
```

- ISR에서는 byte를 queue에 넣고 바로 다시 수신을 겁니다. 출력은 thread에서 합니다.
- UART5 IRQ 우선순위는 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 이하로 설정해 FromISR API를 쓸 수 있게 합니다.
- DBGMCU(device ID)는 debug APB에 있어 `RCC_DBGCFGR.DBGEN`이 켜져 있을 때만 읽습니다.

## 5. A35 ↔ M33 IPC 구성 (`firmware/m33_openamp`, `ipc/`)

```text
Linux userspace     m33_link_ping: frame 작성, CRC 검사, timeout, 통계
        │ open/write/read /dev/ttyRPMSG0
Linux kernel        rpmsg_tty ─ virtio_rpmsg_bus ─ remoteproc(stm32_rproc) ─ stm32-ipcc mailbox   (수정 없음)
        │ vring 2개(각 16칸, 512 B) in DDR reserved-memory      │ IPCC 채널: vring 알림, shutdown
M33 (bare-metal)    OpenAMP(RPMsg/virtio/libmetal) ─ VIRT_UART0 콜백(복사, flag) ─ main loop: link_srv 검사 → PONG/NACK
```

- **resource table**: M33 ELF의 `.resource_table`(0x81200000)에 RPMsg vdev 하나와 vring 2개(16칸, 주소 -1)를 적습니다. Linux가 start 때 vring을 할당해 주소와 status를 이 표에 기록하고, M33은 status를 기다렸다가 vring을 만듭니다.
- **채널**: M33이 endpoint 2개를 `rpmsg-tty` 이름으로 공지하면 Linux가 `/dev/ttyRPMSG0`(0x400), `/dev/ttyRPMSG1`(0x401)을 만듭니다. 같은 주소라도 공장 firmware는 `rpmsg-intc`, `rpmsg_i2c`로 공지하므로 다른 driver가 붙습니다.
- **메시지**: ttyRPMSG0은 m33_link v1(12 B header: magic, ver, type, seq, len, CRC-16; PING → PONG, GET_STATS → STATS, 거부 → NACK), ttyRPMSG1은 ST 예제의 echo입니다. 형식과 측정은 `ipc/README.ko.md`.
- **M33 처리 위치**: IPCC 인터럽트는 flag만 세우고, OpenAMP 처리와 응답은 main loop에서 합니다. 콜백은 vring 칸이 반납되기 전에 데이터를 복사합니다.

| 항목 | 결과 (2026-10-11, 부하 없음, PREEMPT) |
|---|---|
| echo 1000줄 | ttyRPMSG0, ttyRPMSG1 각 1000/1000 |
| round trip p50 / p99 | 0 B 80.1 / 643.0 µs, 32 B 105.2 / 227.2 µs, 484 B 483.7 / 1168.3 µs (각 1000회) |
| 잘못된 frame 6종 | NACK err 1–6, 잘못된 frame 뒤 정상 ping 100/100 |
| 송신 중 M33 stop | 정상 8183, 진행 중 1회 timeout, 이후 write 오류(tty 제거), 잘못된 frame 0, Linux 유지 |
| 재시작 | 1000/1000 |
| GET_STATS | 카운터가 보낸 요청 수와 일치, payload가 붙은 요청은 NACK 4 |

## 6. RTOS → Linux 기능 분리: 기준과 이 구성의 대응

5절의 구성을 "Cortex-A + Cortex-M firmware를 Linux 중심으로 나누는 경우"의 모델로 놓고, 기준마다 이 저장소에서 실제로 한 것과 하지 않은 것을 나눕니다.

| 기준 | 이 구성에서 한 것 | 근거 | 하지 않은 것 |
|---|---|---|---|
| **RTOS에 남길 것**: µs–ms deadline, Linux 재시작과 무관하게 동작, 안전 상태 | 공장 firmware가 USB-PD 상태 머신을 M33에 두고 Linux는 `rpmsg_i2c`로 씀. M33 stop 때 Linux 쪽 `ucsi … error -110`, typec port 사라짐 → 복구 때 돌아옴. 직접 만든 firmware는 100 ms LED 주기를 `osDelayUntil`로 유지(Δtick = host 시간) | 3절, M3 로그 | 제어 loop, 안전 정지 같은 실제 실시간 서비스 |
| **Linux로 옮길 것**: 처리량, 복잡한 스택, 업데이트 | 프로토콜 client, 통계, 측정(CSV)을 Linux userspace에 둠. M33 firmware 교체는 reboot 없이 remoteproc stop/start로 함 | 3절, 5절 | 로깅, 설정, 네트워크 서비스 |
| **kernel vs userspace**: kernel은 interrupt, 공유 메모리, 공통 자원. userspace는 정책, 프로토콜 해석, 재시작 가능한 로직 | transport(remoteproc, virtio_rpmsg_bus, rpmsg_tty, IPCC mailbox)는 kernel 그대로. frame 해석, CRC, timeout, 재시도 판단은 userspace. kernel 수정 없이 시험 가능 | 5절 | `rpmsg_char`(보드에 `virtio0.rpmsg_ctrl.0.0` 있음)나 전용 kernel rpmsg driver 비교. tty line discipline 비용 측정 |
| **인터페이스**: 버전, 고정 header, 길이, sequence, 무결성, timeout | m33_link v1: magic/ver/type/seq/len/CRC-16, NACK 오류 코드와 상대 카운터, client timeout 1 s | `ipc/README.ko.md` | 큰 데이터(메시지당 484 B 초과)를 shared memory + 알림으로 보내는 경로. 여러 요청 동시 처리(pipelining) |
| **검증**: 같은 방법으로 반복 측정, min/p50/p99/max, 조건 명시 | 크기별 1000회 round trip, CSV 보관. 같은 binary를 사용자가 손으로 다시 실행해 p50 재현(0 B 75.4 µs, 32 B 107.8 µs) | 5절, `ipc/README.ko.md` | 부하, CPU 고정, RT 우선순위, PREEMPT_RT 조건. 구간별 시간 분해 |
| **장애 책임**: 아래 계층이 위 계층의 장애를 감지하고 안전 상태 유지 | M33을 송신 중에 멈춰도 Linux는 유지되고 client는 timeout/write 오류로 감지. 잘못된 frame은 M33이 NACK으로 거부하고 계속 동작 | 5절 | M33 crash 감지와 자동 재시작(remoteproc recovery, watchdog), M33 쪽 Linux heartbeat 감시 |

### 이 구성의 한계

- M33이 main loop 하나로 송신과 수신을 함께 처리합니다. 송신 vring이 차면(Linux가 읽지 않으면) `rpmsg_send`가 빈 칸을 기다리는 동안 수신도 멈춥니다. 두 채널도 같은 loop를 쓰므로 ttyRPMSG1 콜백의 UART 출력이 ttyRPMSG0 응답을 늦춥니다.
- tty는 메시지 경계를 RPMsg가 지켜 주기 때문에 쓸 수 있지만, 사용자가 `stty raw -echo`를 빠뜨리면 echo 채널에서 되돌림 loop가 생길 수 있습니다.

### 단계적 분리 순서 (설계, 구현 아님)

| 단계 | 바꾸는 것 | 이유 |
|---|---|---|
| 1 | Linux 쪽 client를 daemon으로 만들고 tty 대신 `rpmsg_char` endpoint 사용 | 메시지 단위 API, line discipline 제거, 재시작 가능한 userspace 소유자 |
| 2 | M33을 FreeRTOS로 옮김: libmetal FreeRTOS 포트, IPCC 콜백 → queue → OpenAMP task 하나가 `OPENAMP_check_for_message()`, 서비스 task는 queue로 메시지를 받음 | 송신 대기와 수신의 결합 제거, 서비스별 우선순위. ST `StarterApp_M33TD`(`Utilities/M33TD_NSAppCore/App/Src/openamp_task.c`)가 같은 구조 |
| 3 | M33 watchdog과 Linux heartbeat, remoteproc crash recovery | 장애 책임 기준의 미검증 항목 |
| 4 | kernel의 여러 소비자가 쓰는 기능만 kernel rpmsg driver로 (공장 firmware의 `rpmsg_i2c` 방식) | 정책은 userspace에 두고, kernel API가 필요한 경우만 kernel로 |
