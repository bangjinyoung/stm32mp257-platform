# Architecture

This page describes the system as it runs on the STM32MP257F-DK. Each section says which statements come from a board run and which are design assumptions.

## 1. Boot chain

| Stage | Runs in | Role | Evidence |
|---|---|---|---|
| ROM | On-chip | Selects fsbl1 on SDMMC1 from the BOOT pins (SD) | `Using SDMMC / Instance 1 / Boot used partition fsbl1` |
| TF-A BL2 | Internal SRAM, EL3 | Loads the DDR firmware, initializes LPDDR4, loads the images below from the FIP | Image IDs and load addresses (`DDR_FW_ID` 0x0e041000, `BL31` 0x0e000000, `BL32_EXTRA1` 0x82000000, `BL33` 0x84000000) |
| TF-A BL31 | EL3 | PSCI, SMC monitor | `Entry point 0xe000000, SPSR 0x3cd` |
| OP-TEE | DDR 0x82000000, S-EL1 | Secure OS and SCP-firmware, an SCMI server for clocks, regulators, and resets | `OP-TEE ST profile: secure_and_system_services`, `SCP-firmware 2.x` |
| U-Boot | DDR 0x84000000, EL1 (v6.0) / EL2 (v6.2.1) | Loads the kernel, initrd, and DTB through extlinux | `SPSR 0x3c5`, extlinux log |
| Linux | 6.6.x, SMP PREEMPT | Starts the M33 firmware through remoteproc | `remoteproc … Booting fw image …` |

- The factory image uses a development security setup (`All debug access are allowed`). It is not a production configuration.
- Boot time on v6.2.1, from 3 soft reboots timed with the host clock, is 1.77 s from TF-A to U-Boot, 6.1 s from U-Boot to the kernel, and 20.1–20.3 s from the kernel to the login prompt. The U-Boot stage includes the 1 s autoboot delay and the extlinux menu wait.

## 2. How the A35 and the M33 split resources

The two cores share the SoC bus and DDR. Hardware decides which core may use each peripheral and each memory region.

- The Resource Isolation Framework (RIF) tags every bus access with a compartment ID (CID). The A35 is CID1 and the M33 is CID2. RIFSC covers peripherals, RISAB and RISAF cover memory, and each GPIO pin has its own setting. Each one lists the CIDs it accepts and blocks the rest (RM0457 §6.2).
- OP-TEE sets up and locks these assignments at boot from the RIF configuration DT (`stm32mp257f-dk-ca35tdcid-ostl-rif.dtsi`). PH6 (LD9) goes to CID2 non-secure, and PH7 (the Linux heartbeat LED1) goes to CID1.
- Each peripheral node in the Linux DT carries `access-controllers = <&rifsc N>`. The M33 uses UART5 (`serial@40110000`), so the Linux DT marks that node `status = "disabled"`.
- Before the M33 BSP touches UART5, it asks the resource manager for access with `ResMgr_Request(RIFSC, UART5)`.
- The Linux DT reserves the DDR regions the M33 needs (code and data, vrings, the resource table) with `reserved-memory`, so Linux never allocates them. Both cores use the IPCC mailbox, each in its own channel direction.

Each peripheral has one owner, and the other core asks the owner over IPC. The factory firmware's USB-PD follows this pattern. The M33 owns UCPD, and Linux reaches it through a virtual I2C device over RPMsg (`rpmsg_i2c`).

## 3. M33 lifecycle (remoteproc)

| Action | Path | Checked on the board |
|---|---|---|
| load / start | Linux `remoteproc` loads the ELF into DDR and releases the M33 reset | Worked 3 times out of 3. Invalid ELFs fail with `-2` or `-22` |
| stop | Linux requests a stop on the IPCC `shutdown` channel. The M33 shutdown thread cleans up and acks, and Linux then resets the M33 | M33 log `[m33] shutdown tick=… led_toggles=…`, then `stopped remote processor m33` |
| restore | Start the default firmware again | `rpmsg_i2c` and `/sys/class/typec/port0` return |

- The M33 firmware reads its run mode with `IS_DEVELOPER_BOOT_MODE()`. Under remoteproc, the A35 side has already set up the clocks. The firmware then only calls `SystemCoreClockUpdate()` and initializes IPCC and CoproSync.
- The remoteproc number can change between boots. The M33 was `remoteproc1` in one boot and `remoteproc0` in another, so scripts look it up by `name`.

## 4. M33 firmware structure (`firmware/m33_freertos`)

```text
UART5 RX IRQ ─► HAL callback ─► osMessageQueuePut ─► console thread ─► printf (UART5 TX)
TIM6 1 kHz ─► HAL tick / FreeRTOS tick
LED thread: osDelayUntil(100 ms) ─► BSP_LED_Toggle(LED3 = PH6)
IPCC shutdown IRQ ─► semaphore ─► shutdown thread ─► UART5 IRQ off, threads exit ─► IPCC ack
```

- The ISR puts the byte in the queue and re-arms reception. The console thread does the printing.
- The UART5 IRQ runs at or below `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`, so the ISR may call FromISR APIs.
- DBGMCU, which holds the device ID, sits on the debug APB. The firmware reads it only when `RCC_DBGCFGR.DBGEN` is set.

## 5. A35 ↔ M33 IPC setup (`firmware/m33_openamp`, `ipc/`)

```text
Linux userspace     m33_link_ping: builds frames, checks CRC, timeout, statistics
        │ open/write/read /dev/ttyRPMSG0
Linux kernel        rpmsg_tty ─ virtio_rpmsg_bus ─ remoteproc(stm32_rproc) ─ stm32-ipcc mailbox   (unmodified)
        │ 2 vrings (16 slots each, 512 B) in DDR reserved-memory   │ IPCC channels: vring notify, shutdown
M33 (bare-metal)    OpenAMP(RPMsg/virtio/libmetal) ─ VIRT_UART0 callback (copy, flag) ─ main loop: link_srv check → PONG/NACK
```

- The M33 ELF has a `.resource_table` section at 0x81200000. It asks for one RPMsg vdev and two vrings of 16 slots, with the address left as -1. At start, Linux allocates the vrings and writes their addresses and a status into the table. The M33 waits for that status and then creates the vrings.
- The M33 announces two endpoints named `rpmsg-tty`, and Linux creates `/dev/ttyRPMSG0` (0x400) and `/dev/ttyRPMSG1` (0x401). The factory firmware announces `rpmsg-intc` and `rpmsg_i2c` at the same addresses, so other drivers bind there.
- ttyRPMSG0 carries m33_link v1. Its 12 B header holds magic, ver, type, seq, len, and CRC-16. The M33 answers PING with PONG, GET_STATS with STATS, and anything it rejects with NACK. ttyRPMSG1 keeps the ST example's echo. `ipc/README.md` has the format and the measurements.
- The IPCC interrupt only sets a flag. OpenAMP processing and the reply run in the main loop. The RPMsg callback copies the data out, because the vring slot goes back to Linux when the callback returns.

| Item | Result (2026-10-11, no load, PREEMPT) |
|---|---|
| Echo, 1000 lines | 1000/1000 on each of ttyRPMSG0 and ttyRPMSG1 |
| Round trip p50 / p99 | 0 B 80.1 / 643.0 µs, 32 B 105.2 / 227.2 µs, 484 B 483.7 / 1168.3 µs (1000 runs each) |
| Six kinds of malformed frame | NACK err 1–6, then 100/100 normal pings |
| M33 stopped while sending | 8183 OK, 1 timeout for the request in flight, then write errors because the tty is gone. 0 malformed frames, and Linux stays up |
| Restart | 1000/1000 |
| GET_STATS | The counters match the requests sent. A request with a payload gets NACK 4 |

## 6. Which functions stay on the RTOS

Section 5 is a small model of a Cortex-A plus Cortex-M system that moves work toward Linux. For each criterion, the table lists what this repository did and what it did not.

| Criterion | What this setup did | Evidence | Not done |
|---|---|---|---|
| Work with µs–ms deadlines, work that must survive a Linux restart, and the safe state stay on the RTOS | The factory firmware keeps the USB-PD state machine on the M33, and Linux uses it through `rpmsg_i2c`. When the M33 stops, Linux logs `ucsi … error -110` and the typec port disappears. Both return on restore. The FreeRTOS firmware in this repository holds a 100 ms LED period with `osDelayUntil`, and its tick tracked the host clock | Section 3, M3 logs | Time-critical services such as a control loop or a safe stop |
| Work that needs throughput, complex stacks, or frequent updates moves to Linux | The protocol client, the statistics, and the CSV measurements live in Linux userspace. remoteproc stop and start replace the M33 firmware without a reboot | Sections 3, 5 | Logging, configuration, network services |
| The kernel takes interrupts, shared memory, and common resources. Userspace takes policy, protocol parsing, and logic that must restart cleanly | The transport (remoteproc, virtio_rpmsg_bus, rpmsg_tty, the IPCC mailbox) is the stock kernel. Frame parsing, CRC, timeout, and retry decisions sit in userspace, so testing needs no kernel change | Section 5 | A comparison with `rpmsg_char` (the board has `virtio0.rpmsg_ctrl.0.0`) or a dedicated kernel rpmsg driver. The cost of the tty line discipline |
| The interface has a version, a fixed header, a length, a sequence number, an integrity check, and a timeout | m33_link v1 has magic, ver, type, seq, len, and CRC-16. A NACK carries an error code and the M33 counters. The client times out after 1 s | `ipc/README.md` | Large data (over 484 B per message) through shared memory plus a notification. Several requests in flight at once |
| Each measurement repeats one method, reports min, p50, p99, and max, and states its conditions | 1000 round trips per payload size, with the CSV kept. A second run by hand with the same binary gave a similar p50 (75.4 µs at 0 B, 107.8 µs at 32 B) | Section 5, `ipc/README.md` | Load, CPU pinning, RT priority, PREEMPT_RT. A breakdown of where the time goes |
| The lower layer detects faults in the upper layer and holds a safe state | Linux stays up when the M33 stops mid-traffic, and the client sees a timeout and then write errors. The M33 rejects malformed frames with a NACK and keeps running | Section 5 | M33 crash detection and automatic restart (remoteproc recovery, watchdog). A Linux heartbeat that the M33 watches |

### Limits of this setup

- One main loop on the M33 does both sending and receiving. If Linux stops reading, the send vring fills, and `rpmsg_send` waits for a free slot. Receiving stops for that wait too. Both channels also share the loop, so the UART print in the ttyRPMSG1 callback delays ttyRPMSG0 replies.
- A tty works here because RPMsg keeps message boundaries. Without `stty raw -echo`, though, the tty echoes received data back, and the echo channel keeps bouncing it between the cores.

### Staged split (design, not implemented)

| Stage | Change | Why |
|---|---|---|
| 1 | Turn the Linux client into a daemon that uses a `rpmsg_char` endpoint instead of the tty | One read per message, no line discipline, and one userspace owner that can restart |
| 2 | Move the M33 to FreeRTOS with the libmetal FreeRTOS port. The IPCC callback posts to a queue, one OpenAMP task calls `OPENAMP_check_for_message()`, and service tasks get messages from queues | A send wait no longer blocks receiving, and each service gets its own priority. ST `StarterApp_M33TD` (`Utilities/M33TD_NSAppCore/App/Src/openamp_task.c`) uses this structure |
| 3 | M33 watchdog, Linux heartbeat, remoteproc crash recovery | These are the open items in the fault row of the table above |
| 4 | Write a kernel rpmsg driver only for functions that several kernel users share, as the factory firmware does with `rpmsg_i2c` | Policy stays in userspace. Code goes into the kernel only when a kernel API needs it |
