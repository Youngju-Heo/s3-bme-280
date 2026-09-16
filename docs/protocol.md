# BME280 로거 시리얼 프로토콜

ESP32-S3-Zero 펌웨어(0.1.0)와 PC 사이의 USB 시리얼 통신 규격이다. `bme280-tool` 외의 클라이언트(다른 언어, 터미널)를 만들거나 디버깅할 때 이 문서를 기준으로 한다.

## 1. 물리 연결과 포트 설정

| 항목 | 값 |
|---|---|
| 연결 | 보드의 USB-C → PC. ESP32-S3 내장 **USB Serial/JTAG** 컨트롤러가 CDC 장치로 동작(별도 USB-UART 칩 없음) |
| Windows 장치 이름 | `USB Serial Device (COMx)` — 이 프로젝트에서는 COM9 |
| 드라이버 | Windows 10/11 기본 CDC 드라이버. 별도 설치 없음 |
| Baud rate | **무관** (USB CDC이므로 어떤 값이든 동일). 관례상 115200 |
| 데이터/패리티/정지 | 8-N-1 (역시 무관) |
| 흐름 제어 | 없음 |
| 인코딩 | ASCII (JSON, 개행 `\n`) |

### DTR / RTS 주의

USB Serial/JTAG는 DTR·RTS 조합으로 칩을 **리셋하거나 다운로드 모드**로 진입시킨다(esptool이 쓰는 기능). 포트를 열 때 DTR/RTS를 토글하면 장치가 재부팅될 수 있으므로 클라이언트는 **포트를 열기 전에 DTR=LOW, RTS=LOW로 고정**한다.

pyserial 예:

```python
import serial
s = serial.Serial()
s.port = "COM9"
s.timeout = 3
s.dtr = False      # open() 전에 설정
s.rts = False
s.open()
s.reset_input_buffer()
```

터미널 프로그램(PuTTY, Tera Term 등)에서는 "DTR/RTS 제어 안 함" 또는 흐름 제어 None으로 두면 된다. 대부분의 터미널은 열 때 DTR을 올리므로 장치가 한 번 재부팅될 수 있는데, 프로토콜 동작에는 지장이 없다(아래 §7 참조).

### 콘솔 로그와의 분리

펌웨어의 ESP-IDF 로그(`ESP_LOGx`)는 **UART0**(GPIO43/44)로 나가고 USB에는 나오지 않는다(`CONFIG_ESP_CONSOLE_SECONDARY_NONE`). 단, ROM/부트로더 로그는 리셋 직후 USB로 나올 수 있다(§7).

## 2. 프레이밍

- 요청: JSON 객체 한 줄 + `\n`. `\r\n`도 허용(`\r`은 개행으로 취급, 빈 줄은 무시).
- 응답: JSON 객체 한 줄 + `\n`. 요청 하나에 응답 하나, 순서대로.
- 요청 줄 최대 **256바이트**(개행 제외). 초과하면 해당 줄은 통째로 버려지고 응답이 없다.
- 요청 JSON은 **평면 객체, 문자열/음이 아닌 정수 값만** 파싱한다. 중첩·배열·이스케이프·공백 외 서식은 지원하지 않는다. 키는 `"key"` 형태로 정확히 따옴표로 감싼다.
- 응답 JSON은 표준 JSON이다. 키는 snake_case.
- 장치는 요청을 순차 처리하며 동시에 하나만 처리한다. 응답을 받은 뒤 다음 요청을 보낸다.

## 3. 명령

모든 응답은 `"ok": true` 또는 `"ok": false`를 가진다. 아래 응답 예시는 실제 출력 그대로의 키 순서다.

### ping — 연결 확인

```
→ {"cmd":"ping"}
← {"ok":true,"firmware":"0.1.0","boot_id":3,"uptime_s":120,"time_valid":false}
```

| 필드 | 설명 |
|---|---|
| `firmware` | 펌웨어 버전 문자열 |
| `boot_id` | 부팅 세대(0~255, 부팅마다 +1, NVS 저장). 미동기 레코드의 시각 환산에 사용 |
| `uptime_s` | 부팅 후 경과 초 |
| `time_valid` | 이번 부팅에서 `set_time`을 받았는지 |

### set_time — 시각 동기화

```
→ {"cmd":"set_time","epoch":1789000000}
← {"ok":true,"boot_id":3,"uptime_s":121}
```

`epoch`: Unix 시각(초, UTC). 장치에 배터리 RTC가 없으므로 **접속할 때마다 보내는 것을 권장**한다. 응답의 `boot_id`·`uptime_s`는 같은 부팅에서 동기화 전에 저장된 레코드의 시각을 PC에서 역산하는 데 쓴다(§5).

### read_now — 즉시 측정 (저장 안 함)

```
→ {"cmd":"read_now"}
← {"ok":true,"temp_c":23.45,"hum_pct":41.20,"pressure_pa":101325}
```

`temp_c`, `hum_pct`는 소수 둘째 자리 고정, `pressure_pa`는 정수(Pa). 측정에 약 20 ms 걸린다.

### get_status — 장치 상태

```
→ {"cmd":"get_status"}
← {"ok":true,"count":1234,"capacity":32512,"interval_s":60,"sensor_ok":true,"store_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121}
```

| 필드 | 설명 |
|---|---|
| `count` | 저장된 레코드 수(CRC 손상 슬롯 포함) |
| `capacity` | 고정 용량 32,512 |
| `interval_s` | 현재 측정 주기 |
| `sensor_ok` | 마지막 주기 측정이 성공했는지 (부팅 시 센서 초기화 실패면 항상 false) |
| `store_ok` | 마지막 플래시 저장이 성공했는지 |

### get_log — 이력 조회 (페이지)

```
→ {"cmd":"get_log","offset":0,"limit":500}
← {"ok":true,"total":1234,"offset":0,"records":[[1789000000,2345,4120,101325,1,3],[1789000060,2350,4118,101320,1,3]]}
```

| 파라미터 | 기본 | 설명 |
|---|---|---|
| `offset` | 0 | 0 = 가장 오래된 레코드. `total` 이상이면 빈 배열 |
| `limit` | 100 | 최대 **500**. 초과 값은 500으로 잘림 |

- `records`의 각 원소는 배열 `[timestamp, temp_centi, hum_centi, pressure_pa, flags, boot_id]` (§4).
- **페이지네이션은 `offset += limit`으로 진행**하고 `offset >= total`이면 끝낸다. CRC가 깨진 슬롯은 건너뛰므로 한 페이지에 `limit`보다 적은 레코드가 올 수 있다 — 받은 개수로 offset을 올리면 안 된다.
- 500건 응답은 약 20 KB이며 장치가 32건씩 스트리밍하므로 수신 타임아웃은 넉넉히(권장 10초) 둔다.

### clear_log — 이력 전체 삭제

```
→ {"cmd":"clear_log"}
← {"ok":true}
```

512 KB 파티션 128섹터를 모두 소거하므로 **응답까지 수 초** 걸린다(권장 타임아웃 30초). 그동안 주기 측정도 멈춘다.

### set_interval — 측정 주기 변경

```
→ {"cmd":"set_interval","interval_s":60}
← {"ok":true}
```

10~3600초. NVS에 저장되어 재부팅 후에도 유지된다. 새 주기는 마지막 측정 시점 기준으로 즉시 적용된다.

### 오류 응답

```
← {"ok":false,"error":"unknown_cmd"}
```

| `error` | 원인 |
|---|---|
| `bad_request` | `cmd` 문자열이 없거나, 필수 파라미터(`epoch`, `interval_s`)가 없거나 정수가 아님(음수, 2³²−1 초과, JSON 형식 불량 포함) |
| `unknown_cmd` | 모르는 `cmd` |
| `sensor_error` | 센서 초기화 실패 상태이거나 측정 중 SPI 오류/타임아웃 |
| `out_of_range` | `interval_s`가 10~3600 밖 (NVS 쓰기 실패도 이 코드로 나옴) |
| `store_error` | `clear_log` 중 플래시 소거 실패 |

## 4. 레코드 형식

장치는 레코드를 16바이트 고정 길이로 플래시에 저장하고, `get_log`에서는 같은 필드를 JSON 배열로 보낸다.

| 순서 | 필드 | 저장 타입 | 의미 |
|---|---|---|---|
| 0 | `timestamp` | u32 | `flags` bit0=1이면 Unix 시각(초), 0이면 **부팅 후 경과 초** |
| 1 | `temp_centi` | i16 | 온도 ×100 (°C). 예: 2345 → 23.45 °C |
| 2 | `hum_centi` | u16 | 습도 ×100 (%RH) |
| 3 | `pressure_pa` | u32 | 기압 (Pa). hPa = ÷100 |
| 4 | `flags` | u8 | bit0 = `time_valid`. 나머지 비트 예약(0) |
| 5 | `boot_id` | u8 | 저장 당시 부팅 세대 |

저장소는 512 KB 파티션을 4 KB 섹터 128개 × 16 B 슬롯 256개로 나눈 링버퍼이며, 항상 1섹터를 소거 상태로 유지해 용량이 정확히 127 × 256 = **32,512건**이다. 가득 차면 가장 오래된 섹터(256건) 단위로 덮어쓴다. 슬롯 끝 2바이트의 CRC-16/CCITT-FALSE로 정전 중 부분 기록을 걸러낸다.

## 5. 시각 해석 규칙 (클라이언트 구현)

접속 직후 `set_time`을 보내고 응답의 `boot_id`(B)·`uptime_s`(U)와 PC 현재시각(T)을 기억한다. 각 레코드에 대해:

| 조건 | 시각 |
|---|---|
| `flags & 1` | `timestamp`를 Unix 시각으로 그대로 사용 |
| 아니고 `record.boot_id == B` | `T − (U − timestamp)` — 같은 부팅이므로 역산 가능(추정, `~` 표시 권장) |
| 아니면 | 절대 시각 불명. `boot#<boot_id> +HH:MM:SS`로 표시 |

`boot_id`는 8비트라 256번 부팅마다 되돌아온다. 오래된 미동기 레코드의 세대 충돌은 실용상 무시한다.

## 6. 타임아웃 권장값

| 명령 | 권장 응답 대기 |
|---|---|
| `ping`, `set_time`, `read_now`, `get_status`, `set_interval` | 3초 |
| `get_log` | 10초 |
| `clear_log` | 30초 |

## 7. 알아 두어야 할 동작

- **리셋 직후 부트 로그 파편**: ROM 부트로더는 USB로 로그를 찍는다. 호스트가 아직 포트를 열지 않은 상태에서 남은 파편이 첫 응답 앞에 같은 줄로 붙어 올 수 있다(`I (102) esp_image: ...{"ok":true,...}`). 펌웨어는 시작 시 `\n`을 한 번 보내 파편을 별도 줄로 끊어 주지만, 클라이언트도 **JSON 파싱에 실패하면 첫 `{`부터 다시 시도**하고, 그래도 안 되면 그 줄을 무시하는 것이 안전하다.
- JSON이 아닌 줄(빈 줄 포함)은 언제든 올 수 있다고 가정하고 무시한다.
- 요청 처리는 100 ms 폴링 루프에서 이뤄지므로 응답 지연은 보통 100 ms 이내다. 주기 측정(약 20 ms)과 겹치면 그만큼 늦어진다.
- 장치 TX 버퍼는 4 KB, RX 버퍼는 512 B다. 응답을 읽지 않고 요청을 연달아 보내면 장치가 TX 버퍼가 빌 때까지 블록되므로 반드시 요청–응답을 번갈아 한다.

## 8. 터미널로 직접 시험하기

PuTTY(Serial, COM9, Flow control None)나 pyserial로 아래를 한 줄씩 보낸다. 줄 끝에 Enter(`\r` 또는 `\n`) 하나면 된다.

```
{"cmd":"ping"}
{"cmd":"read_now"}
{"cmd":"get_log","offset":0,"limit":5}
```

pyserial 한 줄 예:

```python
s.write(b'{"cmd":"read_now"}\n'); print(s.readline().decode())
```

참조 구현: [pc/internal/client/client.go](../pc/internal/client/client.go) (요청/응답·페이지네이션), [pc/internal/records/records.go](../pc/internal/records/records.go) (레코드·시각 해석), [firmware/main/protocol.c](../firmware/main/protocol.c) (장치 측 처리).
