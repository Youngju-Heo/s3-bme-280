# PROJECT_OVERVIEW

## 개요

Waveshare ESP32-S3-Zero + BME280(SPI) 환경 로거. 펌웨어(ESP-IDF, C)가 주기적으로 온도·습도·기압을 측정해 플래시에 32,512건 롤링 저장하고, PC 도구(Go)와 내장 웹 페이지로 조회한다. 배선, 상태 LED, 명령 사용 예는 [README.md](README.md), 시리얼 프로토콜 규격은 [docs/protocol.md](docs/protocol.md)를 참조한다.

## 디렉터리 구조

```
.
├── README.md                 사용자용 안내(배선, LED, 명령)
├── docs/
│   ├── protocol.md           USB 시리얼 JSON 프로토콜 규격
│   └── feature/YYYY-MM/      기능별 설계·계획 문서
├── firmware/                 ESP-IDF 프로젝트
│   ├── main/                 애플리케이션 코드
│   │   └── web/index.html    내장 웹 페이지(단일 파일)
│   ├── components/
│   │   ├── bme280/           센서 드라이버(SPI 레지스터, 보정 계산)
│   │   └── log_store/        플래시 롤링 로그 저장소, 레코드 포맷
│   ├── partitions.csv        파티션 테이블(bmelog 파티션 포함)
│   └── test_host/            호스트 단위 테스트(Unity, fake bus/flash)
└── pc/                       Go CLI(bme280-tool)
    ├── cmd/bme280-tool/      main
    └── internal/             cli, client, records, transport
```

## 주요 구성 요소

### 펌웨어 (`firmware/main`)

| 모듈 | 역할 |
|---|---|
| `app-main.c` | 부팅 순서 조립(설정, SPI/센서 초기화, 로그 저장소, 샘플러, 프로토콜 ops 연결), 메인 루프 |
| `sampler.c` | 주기 측정(실패 시 다음 주기에 재초기화 후 측정), 저장소 기록, 센서·저장소 상태와 센서 실패 원인(rc, chip ID) 보관 |
| `protocol.c` | 줄 단위 JSON 명령 처리(`ping`, `get_status`, `read_now`, `get_log` 등). 하드웨어 접근은 `protocol_ops_t`로 주입 |
| `json-mini.c` | 프로토콜 요청용 최소 JSON 파서 |
| `settings.c` | NVS 설정(측정 주기, boot_id, WiFi 자격증명) |
| `clock.c` | 가동 시간, 시각 동기화 상태 |
| `spi-bus.c` | SPI 버스 초기화와 BME280 버스 어댑터 |
| `status-led.c`, `status-led-color.c` | 내장 RGB LED 상태 표시 |
| `wifi.c` | WiFi 접속, NTP 동기화 |
| `web.c`, `web-bridge.c` | HTTP 서버. `GET /api`를 프로토콜 요청 줄로 변환(읽기 명령만, `set_time`은 POST) |

### 컴포넌트

- `bme280`: 칩 ID 확인, 보정 계수 읽기, 정수 보정 계산. `last_chip_id`로 마지막으로 읽은 chip ID를 기억한다.
- `log_store`: 고정 크기 레코드를 섹터 단위로 순환 기록한다. 플래시 접근은 콜백으로 주입한다.

### PC 도구 (`pc/internal`)

| 패키지 | 역할 |
|---|---|
| `transport` | 시리얼 포트 입출력(테스트용 fake 포함) |
| `client` | 프로토콜 요청/응답, 시각 동기화 |
| `records` | 레코드 해석, 시각 추정(`~`, `boot#N`), CSV 변환 |
| `cli` | 하위 명령(`status`, `now`, `log`, `clear`, `interval`, `wifi`)과 출력 포맷(영어) |

### 웹 (`firmware/main/web/index.html`)

현재값, 장치 상태(센서 실패 원인 포함), 이력 그래프(1시간/1일/1주)를 보여 주는 조회 전용 페이지. 펌웨어에 임베드되어 `GET /`로 제공되고 `GET /api?cmd=…`로 데이터를 가져온다.

## 데이터 흐름

1. `sampler_tick`이 주기마다 `bme280_read`를 호출한다. 성공하면 `log_store_append`로 레코드를 저장하고, 실패하면 결과 코드와 chip ID만 기록한 뒤 다음 주기에 `bme280_init`으로 재초기화하고 측정한다.
2. 조회는 두 경로다. USB Serial/JTAG 위 JSON 프로토콜(`pc` CLI)과 HTTP(`/api`, 웹 페이지). 둘 다 `protocol.c`의 같은 명령 처리기를 거친다.
3. 상태(`get_status`)는 샘플러·저장소·WiFi·시각 정보를 모아 응답하며, LED 색은 센서·저장소·동기화 상태에서 결정된다(표는 README 참조).

## 빌드·테스트·사용법

```powershell
# 펌웨어 빌드/플래시 (ESP-IDF v6.0.1)
. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"
cd firmware
idf.py build
idf.py -p COM9 flash

# 호스트 단위 테스트
powershell -ExecutionPolicy Bypass -File firmware\test_host\run-tests.ps1

# PC 도구
cd pc
go build -o bme280-tool.exe ./cmd/bme280-tool
go test ./...
.\bme280-tool.exe [--port COM9] status
```

전체 명령 목록은 [README.md](README.md)를 참조한다.

## 문서 위치

- 프로토콜 규격: [docs/protocol.md](docs/protocol.md)
- 기능 설계·계획: `docs/feature/YYYY-MM/` (`YYYY-MM-DD-NN-<이름>-design.md`, `-plan.md`)
