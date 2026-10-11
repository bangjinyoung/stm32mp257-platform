# Decisions

| ID | Decision | Why | Considered instead |
|---|---|---|---|
| D-02 | Keep the factory microSD as it is and write v6.2.1 to a second card | The factory card is the baseline for comparing v6.0 with v6.2.1. An image backup of it also exists | Overwriting the factory card |
| D-03 | A35-TD flavor (Cortex-A35 is the trusted domain) | The factory image uses the same flavor, which keeps comparisons direct. The M33 is non-secure, and Linux remoteproc manages it | M33-TD (the M33 handles secure boot) |
| D-05 | Linux loads the M33 firmware directly as an unsigned ELF | This DTB uses `st,stm32mp2-m33`, so Linux loads the ELF itself. The memory layout matches the non-secure layout of the factory firmware | `st,stm32mp2-m33-tee` (OP-TEE verifies the signature and loads it) |
| D-07 | Stop the default USB-PD firmware during tests | While CN15 is unused, the stop touches only USB-C PD. Starting the default firmware again restores it | Replacing everything with the M33-examples image |
| D-08 | Add DT nodes only to the RAM DTB in U-Boot | U-Boot adds the node for a probe test without touching the DTB on the card, and a reboot brings back the original | Replacing the bootfs DTB, overlay |
| D-10 | Fix the v6.2.1 SD problem with a kernel patch and boot the patched kernel by default | Dropping to HS 50 MHz in the DT only works around the cause. The patch is one line, and measurements confirm it | Removing UHS modes in the DT, staying on v6.0 |
| D-11 | Replace the M33 firmware through `/tmp` + `firmware_class.path` | Nothing is written to the SD card, and a reboot brings back the original | Copying to `/lib/firmware` |
| D-12 | Send our own frame format over `rpmsg_tty` | Userspace can test it with no kernel change, and RPMsg already keeps message boundaries | `rpmsg_char` endpoint, dedicated kernel rpmsg driver |
| D-13 | Frames carry magic, version, seq, len and CRC-16 | The vring is shared memory, so bit errors are unlikely. The checks catch firmware built for another protocol version and a writer that ignores the format. Each failure gets its own NACK code | Raw payload without a header, trusting only the RPMsg length |
| D-14 | The M33 RPMsg callback only copies the data and sets a flag. The main loop checks the frame and replies | The vring buffer goes back to Linux when the callback returns. A UART print inside the callback would also add to the round-trip time | Replying directly from the callback (as the ST example does) |
| D-15 | Start the IPC firmware from ST's bare-metal OpenAMP example | With a known-good baseline, a failure points at the protocol change. `docs/architecture.md` §6 has the FreeRTOS plan | Building it on FreeRTOS + an OpenAMP task from the start |
