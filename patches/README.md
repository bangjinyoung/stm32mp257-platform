# patches

## linux/

Base: OpenSTLinux v6.2.1 (v26.06.10) kernel `linux-stm32mp-6.6.129-stm32mp-r3.1`,
the `sources/ostl-linux` tarball plus ST's patch from the Developer Package.

| Patch | Why |
|---|---|
| `0001-mmc-mmci-stm32-run-delay-block-tuning-at-100-MHz-on-.patch` | SDR104 at 100 MHz skipped delay-block tuning on STM32MP25, which gave intermittent wrong reads from a 32 GB SD card. Restores the 50 MHz threshold of r1. |

Apply in the kernel source tree:

```sh
git am <repo>/patches/linux/0001-*.patch    # or: patch -p1 < ...
```
