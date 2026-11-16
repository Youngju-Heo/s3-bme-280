# 센서 실패 원인 노출 설계

## 원 요청

> 지금 장치가 COM9 에 연결되어 있는데, 빨간불이 들어와요.

(진단 결과 `sensor_ok:false`였고, 원인은 UART0 로그로만 나가 COM9에서 볼 수 없었다. "실패 원인을 `get_status`에 노출하도록 펌웨어를 수정할 수 있다"는 제안에 대한 답:)

> 네 진행

(표시 범위 질문에 대한 답:)

> 콘솔에서 그리고 웹에서

(구현 방식 A/B/C 중:)

> A로 진행

## 의도

빨간 LED가 켜졌을 때 USB(COM9)의 `get_status`, `bme280-tool status`, 웹 페이지만으로 센서 실패 원인을 구분한다. 지금은 SPI 초기화 실패, 버스 오류, chip ID 불일치, 측정 타임아웃이 모두 `sensor_ok:false` 하나로 뭉개진다. 특히 실제로 읽힌 chip ID가 있으면 배선 문제를 구분할 수 있다(`0x00`: MISO가 Low에 고정됐거나 전원 없음, `0xFF`: SDO 단선/플로팅, `0x58`: BMP280).

## 목표 / 비목표

- 목표: `get_status`에 원인 문자열과 마지막으로 읽힌 chip ID를 추가하고, CLI와 웹에서 실패 시 함께 표시한다.
- 비목표: 초기화 실패 후 자동 재초기화(지금처럼 리셋해야 복구된다), ESP 로그를 USB로 내보내기, `read_now` 오류 응답 세분화(계속 `sensor_error`로 응답).
- 안티목표: 센서가 정상일 때의 출력이 지저분해지는 것. 정상일 때 CLI와 웹 표시는 지금과 같다.

## 접근 방식

| 안 | 내용 | 판단 |
|---|---|---|
| **A (선택)** | `sensor_error` 문자열 + `sensor_chip_id` 숫자 필드 추가. 드라이버가 마지막 chip ID를 기억 | `wifi_state`·`time_source`와 같은 문자열 관례. chip ID로 배선 문제 구분 가능 |
| B | 숫자 코드 `sensor_rc` 하나만 추가 | 표시하는 쪽마다 매핑이 필요하고 chip ID가 없어 `0x00`/`0xFF` 구분 불가 |
| C | 드라이버 API가 상세 결과 구조체를 반환 | 같은 정보를 얻는 데 호출부·테스트 변경 범위만 큼 |

## 프로토콜

`get_status` 응답의 `store_ok` 뒤에 두 필드를 추가한다.

| 필드 | 값 | 의미 |
|---|---|---|
| `sensor_error` | `none` / `spi_init` / `bus` / `chip_id` / `timeout` | 현재 `sensor_ok` 상태의 원인. `sensor_ok`가 true이면 `none` |
| `sensor_chip_id` | 0~255 정수 또는 `null` | 센서에서 마지막으로 읽힌 chip ID. 한 번도 읽지 못했으면(`spi_init`, 또는 chip ID를 읽기 전 단계의 `bus`) `null` |

- 부팅 시 초기화에 실패하면 그 원인이 리셋 전까지 유지된다(재시도 없음).
- 초기화에 성공한 뒤에는 주기 측정마다 갱신된다. 성공하면 `none`, 실패하면 그 원인이다.
- `read_now`도 드라이버를 통해 chip ID를 읽으므로 `sensor_chip_id`를 갱신할 수 있다. `sensor_error`는 초기화와 주기 측정 결과만 반영한다.

## 표시

정상일 때는 둘 다 지금과 같다. 펌웨어가 구버전이라 필드가 없으면 지금처럼 원인 없이 표시한다.

| `sensor_error` | CLI (`Sensor:` 줄, 영어, 기존 `ok`/`error` 라벨 유지) | 웹 (한국어) |
|---|---|---|
| `chip_id` | `error (chip_id: read 0xFF, expected 0x60)` | `센서 오류 (chip ID 0xFF, 기대값 0x60)` |
| `bus` | `error (bus)` | `센서 오류 (통신 실패)` |
| `timeout` | `error (timeout)` | `센서 오류 (측정 시간 초과)` |
| `spi_init` | `error (spi_init)` | `센서 오류 (SPI 초기화 실패)` |

## 네이밍

| 대상 | 이름 |
|---|---|
| 드라이버 필드 | `bme280_t.last_chip_id` (`int16_t`, 아직 못 읽었으면 -1). `bme280_init` 시작 시 -1로 두고, chip ID 읽기에 성공할 때마다 갱신 |
| 샘플러 | `int sampler_sensor_rc(void)`: 마지막 초기화 또는 주기 측정의 `BME280_*` 결과 코드 |
| 프로토콜 ops | `const char *(*sensor_error)(void *ctx)`, `int (*sensor_chip_id)(void *ctx)` (-1이면 `null` 출력) |
| JSON 키 | `sensor_error`, `sensor_chip_id` |

## 빌더 재량

- 초기화 결과를 샘플러에 넘기는 방식(`sampler_init` 인자 추가 등)과 `spi_init` 판별 위치(app-main)
- 테스트 구성: 기존 `test_host`(Unity)와 `pc/internal/cli` 테스트에 맞춘다.
- 갱신할 문서: `docs/protocol.md` `get_status` 절, README 상태 LED 표의 빨강 행(원인 확인 방법 한 줄)
