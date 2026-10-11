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

## 5. RTOS → Linux migration 분석 (설계, 구현 아님)

이 보드를 "Cortex-A + Cortex-M 이기종 firmware를 Linux 중심으로 옮기는 경우"의 모델로 놓고 정리한 기준입니다. 아래 표는 측정 결과가 아닙니다.

| 질문 | 기준 | 이 보드에 적용 |
|---|---|---|
| RTOS(M33)에 남길 것 | µs–ms 단위 deadline, Linux 재시작과 무관하게 동작, 안전 상태 진입 | 액추에이터 제어 loop, watchdog 응답, fault 감지 후 안전 정지, 전원 관련 상태 머신 (예: USB-PD) |
| Linux로 옮길 것 | 처리량, 복잡한 스택, 잦은 업데이트, 파일과 네트워크 | 설정, 로깅, 통신 스택, 진단과 업데이트, 상위 제어 |
| kernel vs userspace | kernel: interrupt, DMA, 레지스터, 여러 driver가 쓰는 공통 자원. userspace: 정책, 프로토콜 해석, 재시작 가능한 로직 | IPC transport(rpmsg)와 GPIO IRQ는 kernel, 메시지 해석과 정책은 userspace daemon |
| 인터페이스 | 버전 필드, 고정 header, 길이 검사, sequence, 무결성, timeout | RPMsg 위 메시지, 큰 데이터는 shared memory + 알림 (미검증) |
| 검증 | 같은 방법으로 반복 측정, min/max/p99, 부하 조건 명시 | boot time은 측정함. IPC latency는 다음 단계 |
| 장애 책임 | 아래 계층이 위 계층의 장애를 감지하고 안전 상태 유지 | M33: Linux heartbeat 끊김 → 안전 상태 (설계). Linux: remoteproc stop/start는 검증함, crash recovery는 미검증 |
