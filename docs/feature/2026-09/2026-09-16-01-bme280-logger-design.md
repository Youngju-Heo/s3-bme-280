# BME280 로거 설계

## 원 요청

> Waveshare ESP32-S3-Zero 보드 COM9 포트에 연결 되어 있는데, 이장비에서 BME-280칩을 이용하여 데이터를 수집하고 관리하는 도구를 만들어서 PC에서 조회를 할 수 있도록 만듭니다. PC조회는 USB연결을 통해서 조회할 수 있으면 좋겠네요

후속 요청(대화 중 확정):

> 장치 이력은 정확하게 고정 크기를 사용하고, rolling 되도록 했으면 합니다.

> 이번에 작업에서는 SPI 통신 테스트도 해볼겸 해서 SPI 로 연결 하면 좋겠어요.

## 확정된 요구사항

| 항목 | 결정 | 근거 |
|---|---|---|
| 펌웨어 | ESP-IDF v6.0.1 (`C:\esp\v6.0.1\esp-idf`) | 사용자 지정. `idf.py`로 빌드/플래시 자동화 |
| 센서 연결 | SPI2(FSPI), 모드 0, 1MHz, SCK=GPIO12 / MOSI=GPIO11 / MISO=GPIO13 / CS=GPIO10 | 사용자 요청(SPI 테스트). FSPI 기본 IO_MUX 핀. 점퍼선 배선을 고려해 보수적인 클럭 |
| 이력 저장 | 장치 플래시에 고정 크기 롤링 링버퍼, 정확히 32,512건 | 전원 차단에도 이력 유지. 가득 차면 가장 오래된 것부터 덮어씀 |
| 측정 주기 | 기본 60초, PC 명령으로 10~3600초 변경(NVS 저장) | 1분 주기 시 약 22일분 보관 |
| 시간 | PC CLI 접속 시마다 PC 시각으로 동기화. 미동기 레코드는 부팅 경과초 + 플래그로 저장 | 보드에 배터리 RTC 없음. 외부 RTC 없이 실용적 |
| PC 도구 | Python 3.12 CLI (`bme280-tool`), 의존성 pyserial만 | 자동화·테스트 용이 |
| PC↔장치 | USB Serial/JTAG(COM9) 위에 줄 단위 JSON | 터미널로 직접 디버깅 가능, 구현 단순 |

## 전체 구조

```
[BME280] --SPI--> [ESP32-S3-Zero 펌웨어] --USB CDC(COM9)--> [PC: bme280-tool]
                    ├ sampler   주기 측정 → log_store 저장
                    ├ log_store raw 파티션 롤링 링버퍼
                    ├ clock     동기 시각 / 부팅 경과 시간 / boot_id
                    ├ settings  NVS: 측정 주기
                    └ protocol  JSON 줄 명령 처리
```

```
firmware/                ESP-IDF 프로젝트
  CMakeLists.txt
  partitions.csv         bmelog 파티션(512KB) 포함
  sdkconfig.defaults     USB Serial/JTAG 콘솔, 로그는 UART0
  main/                  app-main.c, sampler.c, protocol.c, clock.c, settings.c, spi-bus.c
  components/
    bme280/              센서 드라이버(버스 추상화, 보정 계산)
    log_store/           링버퍼(플래시 접근 추상화)
  test_host/             gcc + Unity: bme280 보정 / log_store 테스트
pc/                      Python 패키지 bme280_tool
  pyproject.toml
  src/bme280_tool/       transport.py, client.py, records.py, cli.py
  tests/
docs/feature/2026-09/
```

## 펌웨어

### bme280 컴포넌트

- 버스 접근은 함수 포인터 구조체로 주입: `bme280_bus_t { read_regs(ctx, reg, buf, len); write_reg(ctx, reg, val); }`. 보정 계산은 순수 C로 두어 호스트 테스트.
- 초기화: 소프트 리셋(0xE0 ← 0xB6) → chip id(0xD0) == 0x60 확인 → 보정 데이터(0x88~0xA1, 0xE1~0xE7) 읽기 → 설정(`osrs_t/p/h` ×1, forced 모드 사용).
- 측정: forced 모드 트리거 → 측정 완료 대기(`status.measuring` 폴링, 최대 100ms) → raw 읽기(0xF7~0xFE) → 데이터시트 보정식(32비트 정수 버전)으로 온도(0.01°C), 기압(Pa), 습도(0.01%) 산출.
- SPI 규약: 레지스터 주소 bit7 = 읽기 1 / 쓰기 0. `spi-bus.c`가 IDF `spi_master`로 `bme280_bus_t`를 구현.

### log_store 컴포넌트

**레코드 형식 (16바이트 고정, little-endian)**

| 오프셋 | 타입 | 필드 | 설명 |
|---|---|---|---|
| 0 | u32 | `timestamp` | `time_valid`면 Unix epoch(초), 아니면 부팅 후 경과 초 |
| 4 | i16 | `temp_centi` | 온도 ×100 (°C) |
| 6 | u16 | `hum_centi` | 습도 ×100 (%) |
| 8 | u32 | `pressure_pa` | 기압 (Pa) |
| 12 | u8 | `flags` | bit0 = `time_valid` |
| 13 | u8 | `boot_id` | 부팅 세대(mod 256). 미동기 레코드의 상대 시간 소속 구분 |
| 14 | u16 | `crc16` | 앞 14바이트 CRC-16/CCITT-FALSE |

**레이아웃**: 파티션 `bmelog` 512KB = 128섹터(4KB) × 256슬롯. 빈 슬롯은 16바이트 모두 `0xFF`(소거 상태). 항상 1섹터 이상을 소거 상태로 유지하므로 용량은 127 × 256 = **32,512건 고정**.

**동작**

- 쓰기: head 슬롯에 기록 후 head++. head가 섹터 경계를 넘어 새 섹터에 진입하기 전에 **그 다음 섹터를 소거**(erase-ahead). 소거되는 섹터가 tail을 포함하면 tail을 그 다음 섹터 첫 슬롯으로 이동 → 롤링.
- 부팅 복구: 128섹터의 첫 4바이트를 검사해 소거된 섹터(`0xFFFFFFFF`)를 찾음. 소거 섹터 구간 바로 다음 섹터의 첫 슬롯 = tail. 소거 섹터 구간 직전 섹터에서 첫 빈 슬롯 = head. 전 섹터가 소거 상태면 head = tail = 0.
- 읽기: `offset`(0 = 가장 오래된 것)과 `limit`로 슬롯 인덱스를 산술 계산. CRC 불일치 레코드는 건너뜀(정전 중 부분 쓰기 대응).
- 전체 삭제: 파티션 전체 소거, head = tail = 0.
- 플래시 접근은 `log_store_flash_t { read, write, erase_sector }` 함수 포인터로 주입. 타깃에서는 `esp_partition_*`, 호스트 테스트에서는 RAM 배열.

### clock / settings / sampler / protocol

- `clock`: `set_time(epoch)` 시 `settimeofday`. `boot_id`는 NVS 카운터로 부팅마다 +1. 미동기 상태에서는 `esp_timer_get_time()` 기반 경과초 제공.
- `settings`: NVS 네임스페이스 `bme`, 키 `interval_s`, `boot_id`.
- `sampler`: FreeRTOS 태스크. 주기마다 `bme280_read` → `log_record_t` 생성 → `log_store_append`. 센서 오류 시 저장 생략, 상태 플래그(`sensor_ok=false`) 갱신.
- `protocol`: USB Serial/JTAG에서 한 줄(최대 256B) 읽어 cJSON으로 파싱, 명령 디스패치, 한 줄 응답. 동기 접근을 위해 `log_store`는 뮤텍스로 보호.
- IDF 로그는 UART0로 라우팅(`sdkconfig.defaults`), USB 포트는 프로토콜 전용.

## 프로토콜 (줄 단위 JSON, 키는 snake_case)

| 요청 | 응답 |
|---|---|
| `{"cmd":"ping"}` | `{"ok":true,"firmware":"0.1.0","boot_id":3,"uptime_s":120,"time_valid":false}` |
| `{"cmd":"set_time","epoch":1789000000}` | `{"ok":true,"boot_id":3,"uptime_s":121}` |
| `{"cmd":"read_now"}` | `{"ok":true,"temp_c":23.45,"hum_pct":41.2,"pressure_pa":101325}` (즉시 측정, 저장 안 함) |
| `{"cmd":"get_status"}` | `{"ok":true,"count":1234,"capacity":32512,"interval_s":60,"sensor_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121}` |
| `{"cmd":"get_log","offset":0,"limit":500}` | `{"ok":true,"total":1234,"offset":0,"records":[[timestamp,temp_centi,hum_centi,pressure_pa,flags,boot_id],...]}` |
| `{"cmd":"clear_log"}` | `{"ok":true}` |
| `{"cmd":"set_interval","interval_s":60}` | `{"ok":true}` (10~3600 범위 밖이면 오류) |
| 오류 | `{"ok":false,"error":"unknown_cmd" \| "bad_request" \| "sensor_error" \| "out_of_range"}` |

- `get_log`의 `offset`은 가장 오래된 레코드 = 0. `limit` 최대 500. 레코드는 배열 형태로 크기 절감.
- PC는 JSON으로 파싱되지 않는 줄을 무시한다.

## PC CLI

```
bme280-tool [--port COM9] <command>
  status                             장치 상태
  now                                즉시 측정값
  log [--last N] [--since ISO8601] [--csv PATH]
                                     이력 조회(기본 표 출력, 최신순, 페이지네이션 자동)
  clear [--yes]                      이력 삭제(확인 프롬프트)
  interval [SECONDS]                 측정 주기 조회/변경
```

- 접속 직후 항상 `set_time` 전송(동기화).
- 미동기 레코드 표시: 응답의 `boot_id`가 현재 세대와 같으면 `PC 현재시각 - (uptime_s - timestamp)`로 환산하고 `~` 접두. 다른 세대면 `boot#N +HH:MM:SS`.
- 모듈: `transport.py`(pyserial 래핑, 줄 송수신, 타임아웃) / `client.py`(명령 → dict, 오류 → `DeviceError`) / `records.py`(레코드 디코딩·시간 환산) / `cli.py`(argparse, 표·CSV 출력).
- 사용자 노출 메시지는 한국어, 로그·예외 메시지는 영어.

## 테스트

- `firmware/test_host/`: MinGW gcc(`C:\msys64\mingw64\bin\gcc`) + IDF `components/unity`.
  - `log_store`: 빈 상태 / 순차 쓰기 / 섹터 경계 erase-ahead / 랩어라운드 후 tail 이동 / 부팅 복구(랩 전·후) / CRC 손상 레코드 스킵 / 전체 삭제.
  - `bme280`: 데이터시트 보정 예제 값으로 온도·기압·습도 계산 검증, chip id 불일치 시 실패.
- `pc/tests/`: 가짜 트랜스포트로 `client`·`records`·`cli` 검증(pytest).
- 하드웨어 통합(수동): 플래시 후 `bme280-tool status` → `now` → 수 분 대기 → `log --last 5` → `clear` 확인.

## 네이밍

| 대상 | 이름 |
|---|---|
| 펌웨어 컴포넌트 | `bme280`, `log_store` |
| main 소스 파일 | `app-main.c`, `sampler.c`, `protocol.c`, `clock.c`, `settings.c`, `spi-bus.c` |
| 핵심 타입 | `log_record_t`, `log_store_t`, `log_store_flash_t`, `bme280_t`, `bme280_bus_t`, `bme280_reading_t` |
| 핵심 함수 | `log_store_init/append/read/count/clear`, `bme280_init/read` |
| 파티션 | `bmelog` (type=data, subtype=0x40, 512KB) |
| NVS | 네임스페이스 `bme`, 키 `interval_s`, `boot_id` |
| Python 패키지 / CLI | `bme280_tool` / `bme280-tool` |
| Python 클래스 | `SerialTransport`, `DeviceClient`, `DeviceError`, `LogRecord` |
