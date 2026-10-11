# v6.2.1 SD corruption: SDR104 delay-block tuning never ran

The OpenSTLinux v6.2.1 Starter image on the STM32MP257F-DK corrupted data read from the SD card now and then. This page records how the cause was found and fixed.

## Symptoms

- The same 32 GB SanDisk Ultra card with v6.2.1 failed differently on each boot.
  - `Oops - Undefined instruction` at 5.2 s
  - A kernel panic at 6.9 s when init died with SIGILL
  - The console stopped after a udev timeout
- A readback of the card matched the image file.
- The factory v6.0 image booted fine from the same card. That ruled out the card and pointed at the v6.2.1 software.

## Ruling things out

Each test used the same card and image and changed one boot condition. All changes were made in U-Boot, in RAM only, and nothing was written to the card.

| Test | SD mode | cpufreq | KVM (EL2) | Result |
|---|---|---|---|---|
| File crc32 and DDR mtest in U-Boot | U-Boot HS | n/a | n/a | crc32 matched the reference 3 times and mtest found 0 errors, so DDR and the stored data are fine |
| A | HS 50 MHz (UHS properties removed in the DT) | off | off | Login, md5 matched 3 times, 0 Oops |
| B | SDR104 | off | off | Panic when init died with SIGILL |
| C | HS 50 MHz | on | on | Login, md5 matched 3 times, 0 Oops |

These runs show that data goes bad only when Linux reads in SDR104 mode. At HS 50 MHz the reads are correct whatever the CPU frequency and EL2 settings are.

## Cause

The kernel source of v6.0 (r1) and v6.2.1 (r3.1) differ in the tuning threshold, and dynamic debug showed that tuning never ran.

- In `drivers/mmc/host/mmci_stm32_sdmmc.c`, r3.1 sets `dlyb->min_freq` to 100 MHz for `st,stm32mp25-sdmmc2`. r1 used 50 MHz.
- `sdmmc_execute_tuning()` returns without tuning when `actual_clock <= min_freq`.
- `/sys/kernel/debug/mmc*/ios` on the board shows a requested clock of 166 MHz, an **actual clock of 100 MHz**, SDR104 timing, and 1.8 V.
- Booting with `dyndbg="file mmci_stm32_sdmmc.c +p"` printed no `unit: max_dly: phase:` line from `sdmmc_dlyb_phase_tuning`. The delay block stayed off, so the controller sampled RX at 100 MHz with no phase correction.
- In that state, the md5 of one file differed in 1 read out of 3. The corruption is intermittent and happens on read.

## Fix and verification

`patches/linux/0001-mmc-mmci-stm32-run-delay-block-tuning-at-100-MHz-on-.patch` sets `min_freq` back to 50 MHz. The patch changes one line.

| Check | Result |
|---|---|
| Tuning runs | `48220000.mmc (SD): unit:0 max_dly:32 phase:24`, and the eMMC logs `phase:21` |
| Bus mode | SDR104 at an actual 100 MHz and 1.8 V, the same as before |
| Repeated md5 after `drop_caches` | systemctl 20/20 and libc 20/20 match the reference |
| Load | md5 of a 256 MB urandom file and of a 331 MB tar of `/usr/lib` matched 3/3 |
| Default boot | extlinux boots the patched kernel by default. 3 reboots gave 3/3 logins and 0 Oops |

The before and after runs used the same card, the same DTB, and the same SDR104 mode. Before the patch, no tuning log appeared, md5 failed 1 time in 3, and init panicked. After the patch, tuning ran and md5 matched 20 times out of 20.

## Open questions

- Why the controller reported no CRC error while it returned wrong data.
- Whether ST raised the threshold to 100 MHz on purpose, and how other cards and boards behave. The next step is to look for an existing ST report and file one if there is none.

## What this debugging showed

- Swapping only the software version on the same card separates a hardware cause from a software cause in a few boots.
- "The file is corrupt" has two halves, the stored data and the data read back. U-Boot crc32 checked the first half, and repeated md5 in Linux checked the second.
- The fix is one line. What justifies it is three measurements: the actual clock in `ios`, the missing dynamic debug log, and the repeated md5.
