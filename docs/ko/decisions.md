# Decisions

| ID | 결정 | 이유 | 고른 것 대신 고려한 것 |
|---|---|---|---|
| D-02 | 출고 microSD는 그대로 두고, v6.2.1은 두 번째 카드에 기록 | 출고 상태를 기준선으로 남겨 v6.0 / v6.2.1 비교가 가능해야 함. 출고 카드는 이미지로도 백업 | 출고 카드에 덮어쓰기 |
| D-03 | A35-TD flavor (Cortex-A35가 trusted domain) | 출고 이미지와 같은 구성이라 비교가 쉬움. M33은 non-secure로 Linux remoteproc이 관리 | M33-TD (M33이 secure 부팅을 맡는 구성) |
| D-05 | M33 firmware는 서명 없는 ELF로 Linux가 직접 load | 이 DTB의 `st,stm32mp2-m33`는 Linux가 ELF를 load함. 공장 firmware와 같은 NS 메모리 배치 | `st,stm32mp2-m33-tee` (OP-TEE가 서명 검증 후 load) |
| D-07 | 시험 중 기본 USB-PD firmware 정지 | CN15를 쓰지 않는 동안 영향이 USB-C PD에 한정됨. 언제든 기본 firmware로 복구 가능 | M33-examples 이미지로 전체 교체 |
| D-08 | DT 노드는 U-Boot에서 RAM DTB에만 추가 | 카드의 DTB를 바꾸지 않아도 driver probe를 시험할 수 있고, 재부팅하면 원래대로 돌아감 | bootfs DTB 교체, overlay |
| D-10 | v6.2.1 SD 문제는 kernel 패치로 수정하고 기본 부팅을 패치 kernel로 변경 | DT로 HS 50 MHz로 낮추는 방법은 우회일 뿐 원인을 고치지 않음. 패치는 한 줄이고 측정으로 검증됨 | DT에서 UHS 모드 제거, v6.0 유지 |
| D-11 | M33 firmware 교체는 `/tmp` + `firmware_class.path` | SD에 쓰지 않고 재부팅하면 원래대로 돌아감 | `/lib/firmware`에 복사 |
| D-12 | IPC는 `rpmsg_tty` 위에 자체 frame을 둠 | kernel 수정 없이 userspace에서 시험 가능. 메시지 경계는 RPMsg가 지켜 줌 | `rpmsg_char` endpoint, 전용 kernel rpmsg driver |
| D-13 | frame에 magic, version, seq, len, CRC-16을 둠 | vring은 공유 메모리라 전송 오류보다 버전이 다른 firmware나 잘못된 writer를 거르는 용도. 오류마다 NACK 코드로 원인을 구분 | 헤더 없이 raw payload, RPMsg 길이만 신뢰 |
| D-14 | M33은 RPMsg 콜백에서 복사와 flag만, 검사와 응답은 main loop에서 | 콜백이 끝나면 vring 버퍼가 반납됨. 콜백 안의 UART 출력이 왕복 시간에 더해지지 않게 함 | 콜백에서 바로 응답 (ST 예제 방식) |
| D-15 | IPC firmware는 ST bare-metal OpenAMP 예제에서 시작 | 동작이 확인된 기준선에서 프로토콜만 바꿔 원인 분리가 쉬움. FreeRTOS 이전 계획은 `docs/ko/architecture.md` §6 | FreeRTOS + OpenAMP task로 바로 구성 |
