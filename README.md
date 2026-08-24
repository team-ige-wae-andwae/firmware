# ESP32-S3 CSI Firmware

ESP32-S3 두 대를 TX와 RX로 사용해 Wi-Fi CSI를 수집하고, Gateway에서 TX의
패킷 전송률과 송신 전력 상한을 런타임에 변경할 수 있는 펌웨어입니다.

## 구성

```text
Gateway PC --USB/UART--> RX --ESP-NOW control--> TX
Gateway PC <--CSI UART-- RX <--Wi-Fi packets----- TX
```

- `csi_send/`: TX 펌웨어. CSI 유도 패킷 송신 및 설정 저장/적용
- `csi_recv/`: RX 펌웨어. CSI 수신, UART 출력, Gateway 명령 중계
- `CONTROL_PROTOCOL.md`: UART 및 ESP-NOW 제어 프로토콜

## 기본 설정

- 패킷 전송률: `40 Hz`
- TX 전력 상한: `20 dBm`
- 허용 전송률: `1..200 Hz`
- 허용 전력 상한: `2..20 dBm`, `0.25 dBm` 단위
- RX UART baudrate: `921600`
- 대상 칩: `ESP32-S3`

`200 Hz`는 설정 허용 상한이며 UART 구간의 무손실 수신을 보장하지 않습니다.
기본 `40 Hz`에서 실제 FPS와 parse error를 확인한 뒤 단계적으로 높이십시오.

## 빌드 및 플래시

ESP-IDF 환경을 연 뒤 각 프로젝트 디렉터리에서 실행합니다. ESP-IDF
`v6.0.1`과 ESP32-S3에서 빌드 및 실기기 동작을 확인했습니다.

Windows에서는 일부 ESP-IDF/CMake 도구가 한글 등 비 ASCII 경로에서
비정상 종료할 수 있습니다. `C:\Espressif\projects\firmware`처럼 영문 경로에
저장소를 복제하는 것을 권장합니다.

TX:

```powershell
cd csi_send
idf.py set-target esp32s3
idf.py build
idf.py -p COM_TX flash monitor
```

RX:

```powershell
cd csi_recv
idf.py set-target esp32s3
idf.py build
idf.py -p COM_RX flash monitor
```

포트 이름은 PC에서 확인한 실제 포트로 바꾸십시오. RX 모니터는
`921600` baudrate를 사용합니다.

## 런타임 제어

Gateway는 RX UART에 `GET`, `SET`, `RESET` 명령을 보내고, RX는 ESP-NOW로
TX에 전달합니다. 설정 적용 성공 여부는 TX가 반환한 `CSI_CTRL_ACK`으로
판정합니다. 상세 형식은 [CONTROL_PROTOCOL.md](CONTROL_PROTOCOL.md)를
참조하십시오.

## 검증 상태

- TX/RX 부팅 및 CSI 수신 확인
- `GET` 응답: `40 Hz / 20 dBm`
- 동일 값 `SET` 후 TX ACK 확인
- 약 `40 FPS` 실수신 확인
- TX와 RX의 기존 플래시 이미지는 로컬에 별도 백업

## 저장소 관리

`sdkconfig.defaults`는 버전 관리하지만 생성된 `sdkconfig`, `build/`, 보드
전체 플래시 백업과 측정 데이터는 커밋하지 않습니다. 배포용 바이너리는
필요한 경우 GitHub Release에 SHA-256과 함께 첨부하십시오.

이 프로젝트는 Espressif의 `esp-csi` 예제를 기반으로 하며 원 라이선스는
[LICENSE](LICENSE)를 따릅니다.
