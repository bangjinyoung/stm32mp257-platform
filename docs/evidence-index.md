# Evidence index

보드에서 실행한 항목과 결과입니다. 상태가 `검증함`인 항목만 보드 로그가 있습니다. 원본 로그와 화면 캡처는 MAC 주소와 보드 serial을 가린 뒤 `evidence/`에 올릴 예정입니다(아직 올리지 않음).

측정 조건: STM32MP257F-DK (MB1605-MP257F-C01, STM32MP257FAK3 Rev.Y), CN21 USB-C 전원(PC), SanDisk Ultra 32 GB microSD, OpenSTLinux v6.2.1 (A35-TD) + 패치 kernel 6.6.129, 상온. 시간은 host에서 VCP 수신 시각으로 기록했습니다(ms 단위, USB 버퍼링 오차는 측정하지 않음).

| ID | 항목 | 상태 | 방법 | 결과 |
|---|---|---|---|---|
| C0.3 | 보드 식별, boot source | 검증함 | 라벨 사진, boot log | MB1605-MP257F-C01, SD boot |
| C1.2 | boot chain 분석 | 검증함 | 출고 v6.0 boot log를 TF-A 소스의 image id, 실행 중 DT 예약 영역과 대조 | `docs/architecture.md` §1 |
| C1.3 | 실행 중 DT 확인 | 검증함 | 실행 중 DTB를 console로 전송(sha256 일치) 후 decompile | model, console, `m33@0` remoteproc 노드 |
| C1.4 | boot 3회, 단계별 시간 | 측정함 | soft reboot 3회, host 시각 | TF-A→login 28.005 / 27.999 / 28.224 s; systemd-analyze 21.248 / 21.276 / 21.467 s |
| C1.5 | v6.2.1 SD 손상 원인과 수정 | 검증함 | 부팅 조건 4가지 비교, ios, dynamic debug, md5 반복 | 패치 후 tuning `phase:24`, md5 20/20 (`docs/sd-sdr104-tuning.md`) |
| C2.1 | module build와 load/unload | 검증함 | vermagic 비교, insmod/rmmod 10회 | 10/10, Oops 0, `greet_count=0` → `-EINVAL` |
| C2.2 | DT probe + GPIO IRQ driver | 검증함 | U-Boot RAM DT에 노드 추가, 버튼 입력 | IRQ 5회 = press 5; debounce 500 ms에서 edge 44 = 20 accepted + 24 ignored (`/proc/interrupts`와 일치) |
| C2.2-N | probe 실패 경로 | 검증함 | 노드 없음, `gpiomon`으로 line 점유, `button-gpios` 없음 | probe 없음 / `-EBUSY` 후 bind로 복구 / `-ENOENT` |
| C3.1 | M33 firmware load/start/stop | 검증함 | remoteproc sysfs, 3회 | 3/3, 기본 firmware 복구 |
| C3.1-N | 잘못된 firmware 거부 | 검증함 | 없는 파일, 19 B 텍스트, 8 KiB random | `-2`, `Image is too small`, `bad magic`; Linux 유지 |
| C3.2 | M33 console, UART RX, LED 100 ms | 검증함 | 키 3개를 5 s 간격으로 전송, tick과 host 시각 비교 | Δtick 5006 / 5005 ms vs host 5.005 / 5.005 s; led_toggles +50 / 5 s; 응답 7–8 ms; stop 시 shutdown 로그 |
| C4.1 | RPMsg TTY echo | 진행 전 | — | — |
| C4.2 | RPMsg round-trip latency | 진행 전 | — | — |

검증하지 않은 것:

- 20 ms debounce에서 실제 접점 bounce (보드에 RC 필터가 있어 관찰되지 않음)
- GPIO 획득 이후 단계(IRQ 요청)의 probe 실패 경로
- `press_count` 읽기와 IRQ handler의 동시성 (unsigned int, lock 없음)
- M33 UART overrun 후 수신 재개 (코드 경로만 있음)
- 패치 kernel에서 SDR104 장시간 부하
- 한 번의 부팅에서만 나온 `videodev` Oops (재현 안 됨, 파일 md5는 정상)
