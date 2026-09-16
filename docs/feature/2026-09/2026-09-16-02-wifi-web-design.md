# WiFi 웹 조회 설계

## 원 요청

> wifi 설정을 통해서 웹으로 정보를 확인 하도록 변경

후속 요청(대화 중 확정):

> 수동 시간 버튼에서는 2가지 모드 가능해야 함: (1)클라이언트 컴퓨터 시간 기반 동기화. (2) 직접입력 동기화

## 확정된 요구사항

| 항목 | 결정 | 근거 |
|---|---|---|
| 웹 내용 | 현재값 + 이력 그래프(1시간/1일/1주) + 장치 상태 | 사용자 선택 |
| WiFi 설정 | USB CLI(Go `bme280-tool`) `wifi <SSID> <PASSWORD>` → 시리얼 `set_wifi` → NVS | "wifi 설정을 통해서". 한글 SSID/비밀번호 허용(UTF-8 바이트 그대로, 규격 상한 32/63B) |
| 접속 주소 | IP 직접 입력. `status`에 IP 표시 | mDNS는 IDF 외부 컴포넌트라 의존 회피 |
| 시간 | WiFi 연결 시 SNTP(`pool.ntp.org`) 자동 동기화. NTP 미동기 시 웹에서 수동 설정 2모드 | 배터리 RTC 없음 문제를 PC 없이 해결 |
| 웹 조작 범위 | 읽기 전용 + `set_time`만 허용. 삭제·주기·WiFi 설정은 USB CLI 전용 | LAN 평문 HTTP, 인증 없음 |
| LED | 기존 의미 유지 + WiFi 접속 중/실패는 파랑 깜빡임 | 사용자 선택 |
| USB 프로토콜 | 그대로 유지(확장만) | WiFi 없는 환경 대응 |

## 핵심 결정

**웹 API = 시리얼 프로토콜의 HTTP 브리지.** `GET /api?cmd=get_log&offset=0&limit=500` 을 펌웨어가 `{"cmd":"get_log","offset":0,"limit":500}` 줄로 바꿔 기존 `protocol_handle_line`에 넘기고, 출력 콜백을 HTTP 청크 전송에 연결한다. 응답 형식·페이지네이션·오류 코드가 시리얼과 동일하고, 이미 호스트 테스트된 로직을 재사용한다. 허용 명령은 화이트리스트: GET `ping`, `read_now`, `get_status`, `get_log`; POST `set_time`. 그 외는 HTTP 403 `{"ok":false,"error":"forbidden"}`.

**동시 접근 = 뮤텍스 하나.** `esp_http_server` 핸들러는 별도 태스크에서 실행되므로 센서·저장소를 보호하는 뮤텍스 1개를 둔다. 메인 루프는 시리얼 한 줄 처리와 `sampler_tick` 각각을 뮤텍스 안에서 수행하고, HTTP 핸들러도 잡고 브리지를 호출한다. `clear_log`(약 5초)는 CLI 전용이라 웹 대기는 실용상 문제없다. 요청 큐로 메인 루프에 위임하는 방식은 코드가 2~3배라 채택하지 않음.

## 구조

```
[BME280]─SPI─┐                                    ┌─USB JSON─── PC bme280-tool (설정·조회)
             ├─[ESP32-S3-Zero]──WiFi STA──공유기──┤
[WS2812 LED]─┘      │                             └─HTTP──────── 브라우저 (조회·시간설정)
                    ├ wifi        NVS 자격증명, 접속/재접속, 상태, IP, SNTP
                    ├ web         esp_http_server: "/"(내장 HTML), "/api"(브리지)
                    ├ web-bridge  쿼리 → 프로토콜 요청 줄 변환 + 화이트리스트 (순수 C)
                    ├ protocol    기존 + set_wifi, get_status 확장
                    └ g_device_lock (센서·저장소 뮤텍스)
```

app_main 초기화 순서: settings → SPI/센서 → log_store → sampler → protocol → USB → LED → `wifi_init()` → `web_start()` → 메인 루프.

## 펌웨어 모듈

### wifi (`wifi.c/.h`, IDF `esp_wifi` `esp_netif` `esp_event` `esp_netif_sntp`)

- `wifi_init()`: `esp_netif_init`, 기본 이벤트 루프, STA netif 생성. NVS에 `wifi_ssid`가 있으면 접속 시작, 없으면 `WIFI_OFF`.
- `wifi_set_credentials(ssid, password)`: NVS(네임스페이스 `bme`, 키 `wifi_ssid`, `wifi_pass`) 저장 후 재접속. `ssid`가 빈 문자열이면 키 삭제 + 연결 해제 + `WIFI_OFF`.
- `wifi_state()` → `WIFI_OFF | WIFI_CONNECTING | WIFI_CONNECTED | WIFI_FAILED`. `wifi_ip(char *buf, size_t len)` → 미연결 시 빈 문자열.
- 이벤트 처리: 끊김/실패 시 재시도, 간격 1→2→4→…→60초(지수 백오프). 30초 이상 미연결이면 `WIFI_FAILED`(재시도는 계속). `IP_EVENT_STA_GOT_IP`에서 IP 저장, `WIFI_CONNECTED`, SNTP 시작. SNTP 동기 콜백에서 `clock_mark_valid(CLOCK_SOURCE_NTP)`.
- 상태 값은 이벤트 태스크에서 갱신되므로 `volatile int`로 두고, IP 문자열은 짧아 별도 잠금 없이 복사(경합 시 최악이 한 번 잘못 표시).

### web (`web.c/.h`, IDF `esp_http_server`)

- `web_start(const web_lock_t *lock)`: 포트 80. `web_lock_t { void *ctx; void (*lock)(void*); void (*unlock)(void*); }`는 app_main이 뮤텍스를 감싸 넘김.
- `GET /` → 내장 `index.html` (`EMBED_FILES`, `text/html; charset=utf-8`).
- `GET|POST /api` → `web_bridge_build_request(method, query, line, sizeof line)` → 실패면 403 → 성공이면 lock → `protocol_handle_line(line, http_chunk_write, req)` → unlock → 청크 종료. `Content-Type: application/json`.
- `http_chunk_write`는 `httpd_resp_send_chunk`. 프로토콜 응답의 끝 `\n`은 그대로 전송(클라이언트는 JSON 파싱만 하면 됨).

### web-bridge (`web-bridge.c/.h`, 순수 C, 호스트 테스트)

- `bool web_bridge_build_request(const char *method, const char *query, char *out, size_t out_len)`
- 쿼리에서 `cmd`, `offset`, `limit`, `epoch`를 읽는다. 값은 `cmd`만 문자열(영문·숫자·`_`), 나머지는 10진 정수만 허용 → 인젝션 여지 없음.
- 화이트리스트: `GET` → `ping read_now get_status get_log`, `POST` → `set_time`. 불일치·미허용 명령·잘못된 값은 false.
- 출력 예: `{"cmd":"get_log","offset":0,"limit":500}` (존재하는 파라미터만 포함).

### protocol 확장

| 요청 | 응답 |
|---|---|
| `{"cmd":"set_wifi","ssid":"우리집","password":"pw12345678"}` | `{"ok":true}`. `ssid` 1~32B, `password` 0B 또는 8~63B 아니면 `out_of_range`; 저장 실패 `store_error`; `ssid` 없으면 `bad_request`. `ssid`가 `""`이면 해제 |
| `{"cmd":"get_status"}` | 기존 필드 뒤에 `"wifi_state":"connected","ip":"192.168.0.23","time_source":"ntp"` 추가. `wifi_state`: `off|connecting|connected|failed`. `time_source`: `none|pc|ntp`. `ip`는 미연결 시 `""` |

`protocol_ops_t`에 `set_wifi(ctx, ssid, password) -> 0|-1(out_of_range)|-2(store_error)`, `wifi_state(ctx) -> const char*`, `wifi_ip(ctx, buf, len)`, `time_source(ctx) -> const char*` 추가.

### json-mini

`json_mini_get_string`이 `\"`와 `\\`를 해석한다(다른 `\` 시퀀스는 실패). 비밀번호에 따옴표·백슬래시가 들어가는 경우 대응. 한글은 바이트 그대로 통과.

### clock 확장

- `clock_source_t { CLOCK_SOURCE_NONE, CLOCK_SOURCE_PC, CLOCK_SOURCE_NTP }`. `clock_set_time`은 PC 출처로 기록, `clock_mark_valid(CLOCK_SOURCE_NTP)`는 SNTP가 `settimeofday`를 이미 했으므로 플래그·출처만 갱신.
- `clock_time_source()` 문자열 반환. NTP가 나중에 오면 PC 값을 덮어씀(NTP 우선).

### status-led 확장

`STATUS_LED_WIFI_CONNECTING` = 파랑 깜빡임(밝기 4, 0.5초). app_main 우선순위: 센서/저장 오류(빨강) → WiFi 설정됐으나 `CONNECTING|FAILED`(파랑 깜빡임) → 시간 미동기(녹색 깜빡임) → 정상(녹색).

### 뮤텍스

`app-main.c`의 `static SemaphoreHandle_t g_device_lock` (recursive 아님). 메인 루프: `protocol_handle_line` 호출과 `sampler_tick` 호출을 각각 감싼다. `web.c`에는 `web_lock_t`로 넘긴다. LED 갱신·시리얼 I/O는 잠금 밖.

## 웹 페이지 (`firmware/main/web/index.html`, 단일 파일, 외부 리소스 없음)

- 상단: 온도·습도·기압 현재값(큰 글씨). 상태 줄: 레코드 수/용량, 주기, 센서·저장소 상태, 시간 출처, IP, 가동 시간. 10초마다 `read_now` + `get_status` 갱신.
- 그래프: `<canvas>` 3개(온도/습도/기압), 기간 버튼 **1시간 / 1일 / 1주**. `get_status.count`와 주기로 시작 offset 계산 → `get_log` 500건씩 순차 로드 → 합침. `flags & 1 == 0`(시각 미확정) 레코드는 제외. 점이 캔버스 폭보다 많으면 구간 평균으로 축소.
- 시간 설정 섹션(`time_source != "ntp"`일 때만 표시):
  1. "이 컴퓨터 시각으로 설정" 버튼 → `Math.floor(Date.now()/1000)`
  2. `<input type="datetime-local">` + "적용" 버튼 → 입력값을 브라우저 로컬 시간대로 해석해 epoch 변환
  두 경우 모두 `POST /api?cmd=set_time&epoch=N`. 성공 시 상태 줄 즉시 갱신.
- 모든 문구 한국어(웹 페이지는 UI이므로 한국어 규칙 적용). vanilla JS.

## PC CLI

Go CLI(`pc/internal/cli`)에 추가. 콘솔 출력은 영어(콘솔 도구 규칙).

- `bme280-tool wifi <SSID> <PASSWORD>` → `set_wifi` → `WiFi credentials saved. Run 'status' to check the connection.`
- `bme280-tool wifi --clear` → `set_wifi` with `ssid=""` → `WiFi credentials cleared.`
- `bme280-tool status` 출력에 `WiFi: connected (192.168.0.23)` / `connecting` / `failed` / `off`, `Time source: ntp | pc | none` 추가.
- 인자 검증(SSID 1~32B, 비밀번호 0 또는 8~63B, UTF-8 바이트 기준)은 포트 접속 전에 수행, 위반 시 종료 코드 2.
- `client.Request`는 `encoding/json`으로 전송 — 한글은 그대로, `"`·`\`만 이스케이프되며 장치 파서(`json-mini`)가 이를 해석한다.

## 설정·빌드

- `main/CMakeLists.txt`: `SRCS`에 `wifi.c web.c web-bridge.c`, `EMBED_FILES "web/index.html"`, `PRIV_REQUIRES`에 `esp_wifi esp_netif esp_event esp_http_server`.
- `sdkconfig.defaults` 추가: `CONFIG_HTTPD_MAX_URI_LEN=512`(쿼리 여유). WiFi 관련은 기본값.
- 앱 파티션 2 MB 유지. 예상 바이너리 1.0~1.3 MB.

## 테스트

- 호스트(gcc+Unity): `json-mini` 이스케이프; `protocol` `set_wifi` 검증·`get_status` 확장 문자열; `web-bridge` 변환·화이트리스트·잘못된 값; `status-led-color` 파랑 깜빡임.
- Go 테스트(`internal/cli`, `internal/client`): `wifi` 명령이 보내는 JSON(한글 SSID 그대로, `"`/`\` 이스케이프), `--clear`, 길이 검증 종료 코드 2, `status` 출력의 WiFi/Time source 줄.
- 실기(수동): `bme280-tool wifi` → `status`에 IP → 브라우저 접속 → 현재값·그래프 3기간 → 시간 설정 2모드(NTP 상태에서는 섹션 숨김 확인은 WiFi 해제 후) → 공유기 전원 차단 시 LED 파랑 깜빡임 → 복구 시 재접속.

## 네이밍

| 대상 | 이름 |
|---|---|
| 새 파일 | `firmware/main/wifi.c/.h`, `web.c/.h`, `web-bridge.c/.h`, `web/index.html`, `test_host/test-web-bridge.c` |
| 타입 | `wifi_state_t`, `web_lock_t`, `clock_source_t` |
| 함수 | `wifi_init/set_credentials/state/ip`, `web_start`, `web_bridge_build_request`, `clock_mark_valid/time_source` |
| NVS 키 | `wifi_ssid`, `wifi_pass` (네임스페이스 `bme`) |
| 프로토콜 | `set_wifi`(`ssid`, `password`), `get_status`의 `wifi_state`, `ip`, `time_source` |
| HTTP | `GET /`, `GET /api?cmd=...`, `POST /api?cmd=set_time&epoch=...` |
| CLI | `bme280-tool wifi <SSID> <PASSWORD>`, `bme280-tool wifi --clear` |
| LED 상태 | `STATUS_LED_WIFI_CONNECTING` |
