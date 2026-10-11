# Evidence index

This page lists each board run with its method and its result. Items marked `Verified` or `Measured` have a board log. The raw logs and screen captures are not public yet. They go to `evidence/` once the MAC address and the board serial are masked.

All runs used an STM32MP257F-DK (MB1605-MP257F-C01, STM32MP257FAK3 Rev.Y) powered from a PC through CN21 USB-C, a SanDisk Ultra 32 GB microSD, and OpenSTLinux v6.2.1 (A35-TD) with the patched kernel 6.6.129, at room temperature. The host stamped each VCP line when it arrived, with ms resolution. The USB buffering error was not measured.

| ID | Item | Status | Method | Result |
|---|---|---|---|---|
| C0.3 | Board identity, boot source | Verified | Board label photo and boot log | MB1605-MP257F-C01, SD boot |
| C1.2 | Boot chain analysis | Verified | Factory v6.0 boot log compared with the image IDs in the TF-A source and with the reserved regions in the running DT | `docs/architecture.md` §1 |
| C1.3 | Running DT | Verified | The running DTB copied over the console (sha256 matched) and decompiled | The model, the console, and the `m33@0` remoteproc node are as expected |
| C1.4 | 3 boots, time per stage | Measured | 3 soft reboots, timed on the host | TF-A→login 28.005 / 27.999 / 28.224 s. systemd-analyze 21.248 / 21.276 / 21.467 s |
| C1.5 | Cause and fix of v6.2.1 SD corruption | Verified | 4 boot conditions compared, `ios` read, dynamic debug, repeated md5 | With the patch, tuning logs `phase:24` and md5 matches 20/20 (`docs/sd-sdr104-tuning.md`) |
| C2.1 | Module build and load/unload | Verified | vermagic compared, insmod and rmmod 10 times | 10/10 with 0 Oops. `greet_count=0` returns `-EINVAL` |
| C2.2 | DT probe + GPIO IRQ driver | Verified | Node added to the RAM DT in U-Boot, then the button pressed | 5 presses gave 5 IRQs. With 500 ms debounce, 44 edges split into 20 accepted and 24 ignored, matching `/proc/interrupts` |
| C2.2-N | Probe failure paths | Verified | No node, the line held by `gpiomon`, and no `button-gpios` | No probe. `-EBUSY`, then bind recovered the driver. `-ENOENT` |
| C3.1 | M33 firmware load/start/stop | Verified | remoteproc sysfs, 3 times | 3/3, and the default firmware came back |
| C3.1-N | Invalid firmware rejected | Verified | A missing file, a 19 B text file, 8 KiB of random data | `-2`, `Image is too small`, and `bad magic`. Linux stays up |
| C3.2 | M33 console, UART RX, LED 100 ms | Verified | 3 keys sent 5 s apart, and the tick compared with host time | Δtick 5006 / 5005 ms against host 5.005 / 5.005 s. led_toggles rose by 50 every 5 s. Replies took 7–8 ms. A stop printed the shutdown log |
| C4.1 | RPMsg channel creation and TTY echo | Verified | dmesg read after starting the M33 OpenAMP firmware, then 1000 numbered lines sent through each channel | `rpmsg-tty` channels at 0x400 and 0x401, `/dev/ttyRPMSG0/1` created, 1000/1000 on each |
| C4.2 | m33_link round-trip latency | Measured | `CLOCK_MONOTONIC` from just before the write until the whole reply is read and checked. 1000 sequential runs per size, no load | p50 / p99 / max in µs. 0 B 80.1 / 643.0 / 1575.4, 32 B 105.2 / 227.2 / 915.1, 484 B 483.7 / 1168.3 / 2649.2 |
| C4.3 | Malformed frames rejected | Verified | One short, magic, version, len, crc and type frame each | NACK err 1–6. The NACK seq is 0 when magic or version is wrong. Then 100/100 normal pings |
| C4.4 | M33 stopped and restarted under traffic | Verified | `echo stop` 1 s into a run of 30000 sends, then start | 8183 OK, 1 timeout for the request in flight, then write errors because the tty is gone. 0 malformed frames, Linux stays up, and 1000/1000 after the restart |
| C4.5 | GET_STATS | Verified | Pings and malformed frames sent between GET_STATS calls, and the counters compared | rx_ok went 1, 102, 3106 and rx_bad went 0 to 8, matching the requests sent. A GET_STATS with a payload gets NACK 4. Pings at 3 sizes and the 6 NACK cases still pass |

These cases were not tested:

- Real contact bounce at 20 ms debounce. The board has an RC filter, and no bounce showed up
- Probe failures after the driver gets the GPIO, such as a failed IRQ request
- Reading `press_count` while the IRQ handler writes it. It is an unsigned int with no lock
- The M33 resuming reception after a UART overrun. Only the code path exists
- A long SDR104 load on the patched kernel
- The `videodev` Oops seen in a single boot. It did not come back, and the file md5 was correct
- Where the IPC time goes (CRC, copies, scheduling), and latency under load, with CPU pinning, or with RT priority
- Several IPC requests in flight at once, and traffic over hours
- M33 crash detection and automatic restart
