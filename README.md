# stm32mp257-platform

This repository brings up Cortex-A35 Linux and Cortex-M33 firmware together on an STM32MP257F-DK board. It covers the boot chain, a GPIO interrupt driver, the M33 firmware lifecycle under Linux remoteproc, and messages between the two cores over RPMsg.

Every result below comes from a run on the board, and the run's log is kept. `Verified` in the tables means exactly that. `Measured` means the run produced numbers and no pass criterion was set.

## Structure

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
 RIF: OP-TEE assigns peripherals and GPIOs to each CID at boot (e.g. PH6/LD9 → M33)
```

The repository has two M33 firmwares, and remoteproc runs one at a time. The figure shows `firmware/m33_freertos`, which drives the console and the LED. The IPC tests ran on `firmware/m33_openamp`, a bare-metal OpenAMP firmware that answers m33_link frames on `/dev/ttyRPMSG0`. `docs/architecture.md` explains both.

## Results

| Area | Item | Status | Key evidence |
|---|---|---|---|
| Boot | Boot chain of the factory image (ROM, TF-A BL2, BL31, OP-TEE, U-Boot, Linux) | Verified | Image IDs and load addresses in the boot log match the TF-A source and the reserved regions in the running DT |
| Boot | OpenSTLinux v6.2.1 Starter on a second microSD | Verified with a kernel patch | The stock kernel corrupts data read from SD. `docs/sd-sdr104-tuning.md` has the cause and the fix |
| Boot | Boot time per stage, 3 runs | Measured | TF-A to login 28.0 / 28.0 / 28.2 s, systemd-analyze 21.2–21.5 s |
| Kernel | Out-of-tree module with matching vermagic, 10 load and unload cycles, bad parameter rejected | Verified | `modules/hello` |
| Kernel | GPIO interrupt platform driver probed from a DT node | Verified | `modules/mp2_button` uses devm GPIO, a threaded IRQ, debounce, and a sysfs `press_count` |
| Kernel | Probe failure with no DT node, a busy GPIO (-EBUSY), or no GPIO property (-ENOENT) | Verified | Linux stays up after the failed probe, and unbind then bind recovers the driver |
| RTOS | Own FreeRTOS firmware loaded, started, and stopped through remoteproc, 3 times | Verified | The factory USB-PD firmware was stopped, replaced, and restored each time |
| RTOS | Missing, too small, and non-ELF firmware rejected | Verified | `Boot failed: -2 / -22`, Linux stays up |
| RTOS | M33 console prints HW info, answers each received character with the uptime, and blinks the LED every 100 ms | Verified | The tick advanced 5006 ms while the host clock advanced 5.005 s, and the LED toggled 50 times in 5 s |
| IPC | RPMsg TTY echo on two channels | Verified | 1000/1000 lines on each of `/dev/ttyRPMSG0` and `/dev/ttyRPMSG1` |
| IPC | m33_link, a frame protocol with magic, version, seq, len, and CRC-16 (PING, PONG, NACK, GET_STATS) | Verified | `ipc/README.md` |
| IPC | Round-trip latency, 1000 runs per payload size | Measured | p50 is 80.1 µs at 0 B, 105.2 µs at 32 B, and 483.7 µs at 484 B, with no load on a PREEMPT kernel |
| IPC | Six kinds of malformed frame, and an M33 stop under traffic | Verified | NACK codes 1–6. No malformed frame reached the client during the stop, Linux stayed up, and 1000/1000 pings passed after the restart |
| IPC | GET_STATS reads the M33 receive counters | Verified | The counters match the requests sent. A GET_STATS with a payload gets a NACK |
| Design | Which functions stay on the RTOS and which move to Linux | Criteria mapped to this setup, later stages are design only | `docs/architecture.md` §6 |

`docs/evidence-index.md` lists every item with its method.

## The v6.2.1 SD corruption

With the ST v6.2.1 image, init died with SIGILL, or the same file gave a different md5 on each read. DDR, CPU frequency, and EL2 each tested clean. The cause was SDMMC delay-block tuning, which never ran in SDR104 mode.

- v6.6-stm32mp-r3.1 raised the tuning minimum frequency for STM32MP25 to 100 MHz.
- SDR104 on this board runs at exactly 100 MHz, so the `actual_clock <= min_freq` check skips tuning.
- A one-line patch restores the r1 threshold of 50 MHz (`patches/linux/0001-…`). With it, the tuning log shows `phase:24` and md5 matches 20 times out of 20.

`docs/sd-sdr104-tuning.md` walks through the investigation.

## Repository layout

```text
firmware/m33_freertos/   M33 FreeRTOS app and Makefile, based on STM32CubeMP2 v1.3.2 (ST files keep their notices)
firmware/m33_openamp/    M33 OpenAMP firmware with the m33_link server on ttyRPMSG0 and echo on ttyRPMSG1, based on ST OpenAMP_TTY_echo
ipc/                     m33_link frame definition (m33_link.h), Linux test tool (linux/m33_link_ping.c), measurements
modules/hello/           Minimal kernel module with a parameter range check
modules/mp2_button/      Platform driver for the USER2 button on PC11
patches/linux/           SDMMC tuning fix for the v6.2.1 kernel
scripts/                 Console file transfer, VCP logger, U-Boot DT node injection, boot time measurement
docs/                    Architecture, decisions, the SD debugging notes, evidence index
docs/ko/                 Korean versions of the docs (ipc/README.ko.md for ipc/README.md)
```

## Build and run

The host is Windows with WSL2 (Ubuntu 22.04), and usbipd attaches the ST-LINK to WSL. The toolchains are the OpenSTLinux v6.2.1 Developer Package SDK, STM32CubeCLT 1.22.0 (GCC 14.3.1), and STM32CubeMP2 v1.3.2.

```sh
# kernel module (in a shell that sourced the SDK environment-setup)
make -C modules/mp2_button KDIR=<kernel build dir>

# M33 firmware
make -C firmware/m33_freertos CUBE=<STM32CubeMP2 v1.3.2>
make -C firmware/m33_openamp CUBE=<STM32CubeMP2 v1.3.2>

# Linux IPC test tool (in a shell that sourced the SDK environment-setup)
make -C ipc/linux

# On the board, replace the M33 firmware.
# The remoteproc number can change between boots, so look it up by name.
M=$(grep -l '^m33$' /sys/class/remoteproc/*/name | xargs dirname)
echo stop > $M/state
echo -n /tmp > /sys/module/firmware_class/parameters/path
echo -n m33_freertos_stripped.elf > $M/firmware
echo start > $M/state

# IPC, with the m33_openamp firmware running
./m33_link_ping -n 1000 -s 32     # round trip
./m33_link_ping -x crc            # a malformed frame gets a NACK
./m33_link_ping -S                # M33 receive counters
```

## Not covered

- A comparison with `rpmsg_char` or a dedicated kernel rpmsg driver, and latency under load, with CPU pinning, or on PREEMPT_RT
- M33 crash detection, automatic restart, and a watchdog
- A full Yocto build and the NPU (X-LINUX-AI)
- Security lifecycle changes such as OTP or secure boot. A separate project covers secure boot on STM32H573 (STiRoT, then MCUboot).

## License

The repository code is MIT (`LICENSE`). The ST files in `firmware/m33_freertos/` and `firmware/m33_openamp/` keep their original notice (`LICENSE-ST.md`). `modules/` is GPL-2.0, and `patches/linux/` follows the Linux kernel license.
