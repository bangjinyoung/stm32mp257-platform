# v6.2.1 SD 읽기 데이터 손상: SDR104 delay-block tuning 누락

STM32MP257F-DK에 OpenSTLinux v6.2.1 Starter 이미지를 올렸을 때 생긴 간헐적 데이터 손상의 원인을 찾고 수정한 기록입니다.

## 증상

- 같은 32 GB SanDisk Ultra 카드에 v6.2.1을 기록하고 부팅하면 증상이 매번 다르게 나타났습니다.
  - 5.2 s에 `Oops - Undefined instruction`
  - init SIGILL로 kernel panic (6.9 s)
  - udev timeout 후 console 정지
- 카드에 기록된 이미지는 readback 비교 결과 원본과 같았습니다.
- 같은 카드에 출고 v6.0 이미지를 올리면 정상이었습니다. 그래서 카드가 아니라 v6.2.1 소프트웨어 쪽 문제로 범위를 좁혔습니다.

## 배제 순서

같은 카드와 이미지로 부팅 조건만 바꿔 가며 확인했습니다. 모든 변경은 U-Boot에서 RAM에만 적용하고 카드에는 쓰지 않았습니다.

| 시험 | SD 모드 | cpufreq | KVM (EL2) | 결과 |
|---|---|---|---|---|
| U-Boot에서 파일 crc32, DDR mtest | U-Boot HS | — | — | crc32 3회 기준값과 일치, mtest 오류 0 → DDR, 카드 데이터 정상 |
| A | HS 50 MHz (DT에서 UHS 속성 제거) | off | off | login, md5 3회 일치, Oops 0 |
| B | SDR104 | off | off | init SIGILL panic |
| C | HS 50 MHz | on | on | login, md5 3회 일치, Oops 0 |

결론(측정): Linux가 SDR104로 읽을 때만 데이터가 깨지고, HS 50 MHz에서는 CPU 주파수나 EL2 설정과 관계없이 정상입니다.

## 원인

v6.0(r1)과 v6.2.1(r3.1)의 kernel 소스를 비교하고, dynamic debug로 tuning 경로를 관찰했습니다.

- `drivers/mmc/host/mmci_stm32_sdmmc.c`: r3.1은 `st,stm32mp25-sdmmc2`의 `dlyb->min_freq`를 100 MHz로 설정합니다. r1은 50 MHz였습니다.
- `sdmmc_execute_tuning()`은 `actual_clock <= min_freq`이면 tuning 없이 return합니다.
- 보드의 `/sys/kernel/debug/mmc*/ios`: clock 166 MHz 요청, **actual clock 100 MHz**, timing SDR104, 1.8 V.
- `dyndbg="file mmci_stm32_sdmmc.c +p"`로 부팅해도 `sdmmc_dlyb_phase_tuning`의 `unit: max_dly: phase:` 로그가 나오지 않았습니다. 즉 delay block이 켜지지 않고, RX 샘플링 시점이 보정되지 않은 채 100 MHz로 읽습니다.
- 이 상태에서 같은 파일 md5가 3회 중 1회 달랐습니다. 손상은 읽을 때마다 간헐적으로 생깁니다.

## 수정과 검증

`patches/linux/0001-mmc-mmci-stm32-run-delay-block-tuning-at-100-MHz-on-.patch`: `min_freq`를 50 MHz로 되돌립니다(한 줄).

| 확인 | 결과 |
|---|---|
| tuning 실행 | `48220000.mmc (SD): unit:0 max_dly:32 phase:24`, eMMC도 `phase:21` |
| 동작 모드 | SDR104, actual 100 MHz, 1.8 V (기존과 같음) |
| md5 반복 (`drop_caches` 후) | systemctl 20/20, libc 20/20 기준값 일치 |
| 부하 | 256 MB urandom 파일, `/usr/lib` tar 331 MB md5 3/3 일치 |
| 기본 부팅 | extlinux 기본 kernel을 패치 kernel로 바꾸고 3회 재부팅, login 3/3, Oops 0 |

같은 카드, 같은 DTB, 같은 SDR104 조건에서 비교했습니다. 패치 전에는 tuning 로그가 없고 md5가 1/3 틀리며 init이 panic했습니다. 패치 후에는 tuning이 실행되고 md5 20/20이 일치했습니다.

## 남은 질문

- 잘못된 데이터를 읽었는데 CRC 오류가 보고되지 않은 이유
- ST가 threshold를 의도적으로 100 MHz로 올렸는지 (다른 카드나 보드에서의 동작). ST에 보고할 예정이며, 기존 보고가 있는지 먼저 확인합니다.

## 배운 점

- 같은 매체에서 소프트웨어 버전만 바꿔 비교하면 하드웨어 원인과 소프트웨어 원인을 빨리 가를 수 있습니다.
- "파일이 깨졌다"는 증상은 저장된 데이터와 읽힌 데이터를 따로 확인해야 합니다. U-Boot crc32와 Linux md5 반복 비교로 둘을 분리했습니다.
- 수정은 한 줄이지만, 근거는 측정입니다: ios의 실제 클럭, dynamic debug 로그, 반복 md5.
