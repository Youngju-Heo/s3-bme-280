# WiFi 웹 조회 구현 계획

## 원 요청

> wifi 설정을 통해서 웹으로 정보를 확인 하도록 변경

후속 요청(대화 중 확정):

> 수동 시간 버튼에서는 2가지 모드 가능해야 함: (1)클라이언트 컴퓨터 시간 기반 동기화. (2) 직접입력 동기화

---

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** USB CLI로 WiFi를 설정하면 장치가 공유기에 접속·SNTP 동기화하고, 브라우저에서 `http://<IP>/`로 현재값·이력 그래프·상태를 보며 NTP 미동기 시 시간을 수동 설정할 수 있게 한다.

**Architecture:** 펌웨어에 `wifi`(STA·재접속·SNTP), `web`(esp_http_server, 내장 HTML), `web-bridge`(URL 쿼리→기존 프로토콜 요청 줄, 순수 C)를 추가하고, HTTP 핸들러가 별도 태스크에서 도니 센서·저장소를 뮤텍스 하나로 보호한다. 프로토콜은 `set_wifi`와 `get_status` 확장, `json-mini`는 `\"`·`\\` 이스케이프 지원. Go CLI에 `wifi` 명령과 status 줄 추가. 웹 페이지는 한국어 UI(단일 HTML, 외부 리소스 없음).

**Tech Stack:** ESP-IDF v6.0.1 (`esp_wifi`, `esp_netif`, `esp_event`, `esp_netif_sntp`, `esp_http_server`), MinGW gcc + Unity 호스트 테스트, Go 1.25 (`go.bug.st/serial`), vanilla HTML/JS.

**Spec:** `docs/feature/2026-09/2026-09-16-02-wifi-web-design.md`

## Global Constraints

- 콘솔(CLI)·펌웨어 로그·오류 문자열은 영어. **웹 페이지 문구는 한국어**(UI). 문서·커밋은 한국어. 커밋 `<type>: <제목>`, 일감번호 없음, `Co-Authored-By` 금지.
- 프로토콜 추가: `set_wifi {ssid(1~32B), password(0 또는 8~63B)}` → `{"ok":true}` / `out_of_range` / `store_error` / `bad_request`(ssid 없음). `get_status` 기존 필드 뒤에 `"wifi_state":"off|connecting|connected|failed","ip":"<dotted or empty>","time_source":"none|pc|ntp"`.
- HTTP: `GET /` → 내장 `index.html`; `GET /api?cmd=ping|read_now|get_status|get_log[&offset=&limit=]`, `POST /api?cmd=set_time&epoch=N`; 그 외 403 `{"ok":false,"error":"forbidden"}`. 응답 `application/json`, 본문은 프로토콜 응답 그대로(`\n` 포함).
- 동시성: `app-main.c`의 뮤텍스 하나가 센서·저장소 접근(시리얼 명령 처리, `sampler_tick`, HTTP 브리지)을 직렬화. LED·USB I/O는 잠금 밖.
- WiFi: STA, NVS 네임스페이스 `bme` 키 `wifi_ssid`/`wifi_pass`, 재접속 백오프 1→2→4→8→16→32→60초, 5회 연속 실패 시 `failed`(재시도 계속). IP 획득 시 SNTP(`pool.ntp.org`) 시작, 동기 콜백에서 `clock_mark_valid(CLOCK_SOURCE_NTP)`. NTP가 유효하면 PC `set_time`은 무시(NTP 우선).
- LED 우선순위: 빨강(센서/저장 오류) → 파랑 깜빡임(WiFi 자격증명 있고 `connecting|failed`) → 녹색 깜빡임(시간 미동기) → 녹색.
- Go CLI 출력 영어: `wifi <SSID> <PASSWORD>` → `WiFi credentials saved. Run 'status' to check the connection.`; `wifi --clear` → `WiFi credentials cleared.`; `status`에 `WiFi: connected (IP)|connecting|failed|off`, `Time source: ntp|pc|none`. SSID 1~32B, 비밀번호 0 또는 8~63B(UTF-8 바이트), 위반 시 종료 코드 2(포트 열기 전).
- 호스트 테스트 `powershell -File firmware\test_host\run-tests.ps1` (`-Wall -Wextra -Werror`), Go `cd pc; go test ./...`, 빌드 `. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"; cd firmware; idf.py build`, 플래시 `idf.py -p COM9 flash`.
- 브랜치 `wifi-web`.

## 파일 구조

```
firmware/main/json-mini.c                          (Task 1 수정: 이스케이프)
firmware/main/clock.h, clock.c                     (Task 2 수정: source)
firmware/main/protocol.h, protocol.c               (Task 2 수정: set_wifi, get_status)
firmware/main/web-bridge.h, web-bridge.c           (Task 3 신규, 순수 C)
firmware/main/status-led-color.h, .c               (Task 4 수정: WIFI_CONNECTING)
firmware/main/settings.h, settings.c               (Task 5 수정: wifi 키)
firmware/main/wifi.h, wifi.c                       (Task 5 신규)
firmware/main/web.h, web.c, web/index.html         (Task 6 신규)
firmware/main/app-main.c, CMakeLists.txt, sdkconfig.defaults   (Task 6 수정)
firmware/test_host/test-json-mini.c, test-protocol.c, test-web-bridge.c, test-status-led.c, run-tests.ps1
pc/internal/client/client.go, cli/cli.go (+tests) (Task 7)
README.md, docs/protocol.md                        (Task 8)
```

---

### Task 1: json-mini 문자열 이스케이프

**Files:**
- Modify: `firmware/main/json-mini.c` (`json_mini_get_string`)
- Test: `firmware/test_host/test-json-mini.c` (테스트 추가)

**Interfaces:**
- Produces: `json_mini_get_string`이 값 안의 `\"`→`"`, `\\`→`\`를 해석. 다른 `\x`는 false. 시그니처 불변.

- [ ] **Step 1: 실패하는 테스트 추가**

`firmware/test_host/test-json-mini.c`의 `main` 앞에 추가하고 `RUN_TEST` 등록:
```c
void test_get_string_unescapes_quote_and_backslash(void) {
    char out[64];
    TEST_ASSERT_TRUE(json_mini_get_string("{\"password\":\"a\\\"b\\\\c\"}", "password", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("a\"b\\c", out);
}

void test_get_string_rejects_unknown_escape(void) {
    char out[64];
    TEST_ASSERT_FALSE(json_mini_get_string("{\"password\":\"a\\nb\"}", "password", out, sizeof out));
}

void test_get_string_keeps_utf8_bytes(void) {
    char out[64];
    TEST_ASSERT_TRUE(json_mini_get_string("{\"ssid\":\"우리집\"}", "ssid", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("우리집", out);
}
```
`test_get_string_too_long`은 그대로 둔다(버퍼 초과 → false).

- [ ] **Step 2: 실패 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-json-mini`
Expected: `test_get_string_unescapes_quote_and_backslash` FAIL (현재는 첫 `"`에서 끊어 `a\`가 나옴)

- [ ] **Step 3: 구현**

`json_mini_get_string`을 다음으로 교체:
```c
bool json_mini_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *p = find_value(json, key);
    if (!p || *p != '"') return false;
    p++;
    size_t n = 0;
    for (;;) {
        char c = *p++;
        if (c == '\0') return false;          // unterminated
        if (c == '"') break;
        if (c == '\\') {
            c = *p++;
            if (c != '"' && c != '\\') return false;   // only \" and \\ are supported
        }
        if (n + 1 >= out_len) return false;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}
```

- [ ] **Step 4: 통과 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1`
Expected: `test-json-mini` 12 Tests 0 Failures, 전체 `ALL PASSED`

- [ ] **Step 5: 커밋**

```bash
git add firmware/main/json-mini.c firmware/test_host/test-json-mini.c
git commit -m "feat: json-mini 문자열 이스케이프(\\\" \\\\) 지원"
```

---

### Task 2: clock 출처 + protocol 확장(set_wifi, get_status)

**Files:**
- Modify: `firmware/main/clock.h`, `firmware/main/clock.c`
- Modify: `firmware/main/protocol.h`, `firmware/main/protocol.c`
- Test: `firmware/test_host/test-protocol.c`

**Interfaces:**
- Produces:
  ```c
  // clock.h
  typedef enum { CLOCK_SOURCE_NONE, CLOCK_SOURCE_PC, CLOCK_SOURCE_NTP } clock_source_t;
  void clock_mark_valid(clock_source_t source);      // 이미 settimeofday된 뒤 플래그/출처만 갱신 (SNTP 콜백용)
  const char *clock_time_source(void);               // "none" | "pc" | "ntp"
  // clock_set_time: source가 NTP면 무시(NTP 우선), 아니면 settimeofday + source PC
  // protocol.h — protocol_ops_t에 추가
  int (*set_wifi)(void *ctx, const char *ssid, const char *password);  // 0 ok, -1 out_of_range, -2 store_error
  const char *(*wifi_state)(void *ctx);                                // "off"|"connecting"|"connected"|"failed"
  void (*wifi_ip)(void *ctx, char *buf, size_t len);                   // dotted or ""
  const char *(*time_source)(void *ctx);                               // "none"|"pc"|"ntp"
  #define PROTOCOL_WIFI_SSID_MAX 32
  #define PROTOCOL_WIFI_PASS_MAX 63
  ```
  `get_status` 응답: 기존 필드 뒤에 `,"wifi_state":"%s","ip":"%s","time_source":"%s"`.

- [ ] **Step 1: 실패하는 테스트 작성**

`firmware/test_host/test-protocol.c` 수정:
- `fake` 구조체에 `char wifi_ssid[33]; char wifi_pass[64]; int set_wifi_rc; const char *wifi_state; const char *wifi_ip; const char *time_source;` 추가.
- 가짜 ops 함수 추가:
```c
static int f_set_wifi(void *c, const char *ssid, const char *password)
{
    (void)c;
    if (fake.set_wifi_rc == 0) { strncpy(fake.wifi_ssid, ssid, 32); fake.wifi_ssid[32] = 0; strncpy(fake.wifi_pass, password, 63); fake.wifi_pass[63] = 0; }
    return fake.set_wifi_rc;
}
static const char *f_wifi_state(void *c) { (void)c; return fake.wifi_state; }
static void f_wifi_ip(void *c, char *buf, size_t len) { (void)c; snprintf(buf, len, "%s", fake.wifi_ip); }
static const char *f_time_source(void *c) { (void)c; return fake.time_source; }
```
- `ops` 초기화에 `.set_wifi = f_set_wifi, .wifi_state = f_wifi_state, .wifi_ip = f_wifi_ip, .time_source = f_time_source` 추가.
- `setUp`에 `fake.wifi_state = "off"; fake.wifi_ip = ""; fake.time_source = "none";` 추가.
- `test_get_status` 기대 문자열을 다음으로 교체:
```c
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"count\":1,\"capacity\":32512,\"interval_s\":60,\"sensor_ok\":true,\"store_ok\":true,\"time_valid\":false,\"boot_id\":3,\"uptime_s\":120,\"wifi_state\":\"off\",\"ip\":\"\",\"time_source\":\"none\"}\n",
        handle("{\"cmd\":\"get_status\"}"));
```
- 테스트 추가(+ `RUN_TEST`):
```c
void test_get_status_reports_wifi_and_time_source(void) {
    fake.wifi_state = "connected"; fake.wifi_ip = "192.168.0.23"; fake.time_source = "ntp";
    const char *out = handle("{\"cmd\":\"get_status\"}");
    TEST_ASSERT_NOT_NULL(strstr(out, "\"wifi_state\":\"connected\",\"ip\":\"192.168.0.23\",\"time_source\":\"ntp\"}\n"));
}

void test_set_wifi_saves_credentials(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}\n", handle("{\"cmd\":\"set_wifi\",\"ssid\":\"우리집\",\"password\":\"pw12345678\"}"));
    TEST_ASSERT_EQUAL_STRING("우리집", fake.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("pw12345678", fake.wifi_pass);
}

void test_set_wifi_clear_with_empty_ssid(void) {
    fake.wifi_ssid[0] = 'x';
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}\n", handle("{\"cmd\":\"set_wifi\",\"ssid\":\"\"}"));
    TEST_ASSERT_EQUAL_STRING("", fake.wifi_ssid);
}

void test_set_wifi_validates_lengths(void) {
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"bad_request\"}\n", handle("{\"cmd\":\"set_wifi\",\"password\":\"pw12345678\"}"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"out_of_range\"}\n", handle("{\"cmd\":\"set_wifi\",\"ssid\":\"abcdefghijklmnopqrstuvwxyz0123456789\",\"password\":\"pw12345678\"}"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"out_of_range\"}\n", handle("{\"cmd\":\"set_wifi\",\"ssid\":\"home\",\"password\":\"short\"}"));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}\n", handle("{\"cmd\":\"set_wifi\",\"ssid\":\"home\",\"password\":\"\"}"));   // open network
    fake.set_wifi_rc = -2;
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"store_error\"}\n", handle("{\"cmd\":\"set_wifi\",\"ssid\":\"home\",\"password\":\"pw12345678\"}"));
}
```

- [ ] **Step 2: 실패 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-protocol`
Expected: 컴파일 오류 (`set_wifi` 멤버 없음)

- [ ] **Step 3: clock 구현**

`firmware/main/clock.h`:
```c
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { CLOCK_SOURCE_NONE, CLOCK_SOURCE_PC, CLOCK_SOURCE_NTP } clock_source_t;

void clock_set_time(uint32_t epoch);           // from the PC; ignored once NTP has synced
void clock_mark_valid(clock_source_t source);  // time already set (e.g. by SNTP); record validity and source
bool clock_time_valid(void);
const char *clock_time_source(void);           // "none" | "pc" | "ntp"
uint32_t clock_uptime_s(void);
uint32_t clock_timestamp(void);                // epoch if valid, else uptime
```

`firmware/main/clock.c`:
```c
#include "clock.h"
#include <sys/time.h>
#include <time.h>
#include "esp_timer.h"

static clock_source_t g_source = CLOCK_SOURCE_NONE;

void clock_set_time(uint32_t epoch)
{
    if (g_source == CLOCK_SOURCE_NTP) return;   // NTP is authoritative once it has synced
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    g_source = CLOCK_SOURCE_PC;
}

void clock_mark_valid(clock_source_t source) { g_source = source; }

bool clock_time_valid(void) { return g_source != CLOCK_SOURCE_NONE; }

const char *clock_time_source(void)
{
    switch (g_source) {
    case CLOCK_SOURCE_PC: return "pc";
    case CLOCK_SOURCE_NTP: return "ntp";
    default: return "none";
    }
}

uint32_t clock_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

uint32_t clock_timestamp(void) { return clock_time_valid() ? (uint32_t)time(NULL) : clock_uptime_s(); }
```

- [ ] **Step 4: protocol 구현**

`firmware/main/protocol.h`: `#define PROTOCOL_WIFI_SSID_MAX 32`, `#define PROTOCOL_WIFI_PASS_MAX 63` 추가, `protocol_ops_t`의 `set_interval_s` 뒤에:
```c
    int (*set_wifi)(void *ctx, const char *ssid, const char *password);   // 0 ok, -1 out of range, -2 store error
    const char *(*wifi_state)(void *ctx);                                 // "off" | "connecting" | "connected" | "failed"
    void (*wifi_ip)(void *ctx, char *buf, size_t len);                    // dotted IPv4 or ""
    const char *(*time_source)(void *ctx);                                // "none" | "pc" | "ntp"
```

`firmware/main/protocol.c`: `cmd_get_status`를 교체하고 `cmd_set_wifi` 추가, 디스패치에 `else if (strcmp(cmd, "set_wifi") == 0) cmd_set_wifi(&w, line);` 추가:
```c
static void cmd_get_status(writer_t *w)
{
    char ip[16] = "";
    g_ops->wifi_ip(g_ops->ctx, ip, sizeof ip);
    emit(w, "{\"ok\":true,\"count\":%lu,\"capacity\":%lu,\"interval_s\":%lu,\"sensor_ok\":%s,\"store_ok\":%s,\"time_valid\":%s,\"boot_id\":%u,\"uptime_s\":%lu,",
         (unsigned long)log_store_count(g_ops->store), (unsigned long)LOG_STORE_CAPACITY,
         (unsigned long)g_ops->interval_s(g_ops->ctx), g_ops->sensor_ok(g_ops->ctx) ? "true" : "false",
         g_ops->store_ok(g_ops->ctx) ? "true" : "false",
         g_ops->time_valid(g_ops->ctx) ? "true" : "false", g_ops->boot_id(g_ops->ctx),
         (unsigned long)g_ops->uptime_s(g_ops->ctx));
    emit(w, "\"wifi_state\":\"%s\",\"ip\":\"%s\",\"time_source\":\"%s\"}\n",
         g_ops->wifi_state(g_ops->ctx), ip, g_ops->time_source(g_ops->ctx));
}

static void cmd_set_wifi(writer_t *w, const char *line)
{
    char ssid[PROTOCOL_WIFI_SSID_MAX + 1];
    char password[PROTOCOL_WIFI_PASS_MAX + 1] = "";
    char big[128];
    if (!json_mini_get_string(line, "ssid", big, sizeof big)) { reply_error(w, "bad_request"); return; }
    size_t ssid_len = strlen(big);
    if (ssid_len > PROTOCOL_WIFI_SSID_MAX) { reply_error(w, "out_of_range"); return; }
    memcpy(ssid, big, ssid_len + 1);
    if (json_mini_get_string(line, "password", big, sizeof big)) {
        size_t pass_len = strlen(big);
        if (pass_len != 0 && (pass_len < 8 || pass_len > PROTOCOL_WIFI_PASS_MAX)) { reply_error(w, "out_of_range"); return; }
        memcpy(password, big, pass_len + 1);
    }
    int rc = g_ops->set_wifi(g_ops->ctx, ssid, password);
    if (rc == -1) { reply_error(w, "out_of_range"); return; }
    if (rc != 0) { reply_error(w, "store_error"); return; }
    emit(w, "{\"ok\":true}\n");
}
```
(`emit`의 내부 버퍼 192B: get_status를 두 번으로 나눠 출력하므로 각 호출이 버퍼 안에 든다.)

- [ ] **Step 5: 통과 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-protocol`
Expected: 13 Tests 0 Failures

- [ ] **Step 6: 커밋**

```bash
git add firmware/main/clock.h firmware/main/clock.c firmware/main/protocol.h firmware/main/protocol.c firmware/test_host/test-protocol.c
git commit -m "feat: set_wifi 명령과 get_status WiFi·시간 출처 필드 추가"
```

---

### Task 3: web-bridge (쿼리 → 프로토콜 요청 줄, 화이트리스트)

**Files:**
- Create: `firmware/main/web-bridge.h`, `firmware/main/web-bridge.c`
- Test: `firmware/test_host/test-web-bridge.c`
- Modify: `firmware/test_host/run-tests.ps1` (항목 추가)

**Interfaces:**
- Produces:
  ```c
  // method: "GET" | "POST"; query: URL 쿼리 문자열("cmd=get_log&offset=0&limit=500") 또는 "".
  // 허용: GET → ping, read_now, get_status, get_log; POST → set_time. 값: cmd는 [a-z_]+, offset/limit/epoch는 10진수만.
  // 성공 시 out에 {"cmd":"get_log","offset":0,"limit":500} 형태(존재하는 파라미터만, 이 순서), true 반환.
  bool web_bridge_build_request(const char *method, const char *query, char *out, size_t out_len);
  ```

- [ ] **Step 1: 실패하는 테스트 작성**

`firmware/test_host/test-web-bridge.c`:
```c
#include "unity.h"
#include "web-bridge.h"

void setUp(void) {}
void tearDown(void) {}

static char out[128];

void test_get_read_only_commands(void) {
    TEST_ASSERT_TRUE(web_bridge_build_request("GET", "cmd=ping", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"ping\"}", out);
    TEST_ASSERT_TRUE(web_bridge_build_request("GET", "cmd=get_log&offset=1000&limit=500", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"get_log\",\"offset\":1000,\"limit\":500}", out);
    TEST_ASSERT_TRUE(web_bridge_build_request("GET", "limit=5&cmd=get_log", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"get_log\",\"limit\":5}", out);
}

void test_post_set_time_only(void) {
    TEST_ASSERT_TRUE(web_bridge_build_request("POST", "cmd=set_time&epoch=1789000000", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"set_time\",\"epoch\":1789000000}", out);
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=set_time&epoch=1", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("POST", "cmd=get_status", out, sizeof out));
}

void test_rejects_forbidden_and_malformed(void) {
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=clear_log", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("POST", "cmd=set_interval&interval_s=10", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("POST", "cmd=set_wifi&ssid=x", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "offset=1", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=get_log&offset=12a", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=get%5Flog", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=ping&unknown=1", out, sizeof out));
    TEST_ASSERT_FALSE(web_bridge_build_request("DELETE", "cmd=ping", out, sizeof out));
}

void test_output_buffer_too_small(void) {
    char tiny[8];
    TEST_ASSERT_FALSE(web_bridge_build_request("GET", "cmd=ping", tiny, sizeof tiny));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_read_only_commands);
    RUN_TEST(test_post_set_time_only);
    RUN_TEST(test_rejects_forbidden_and_malformed);
    RUN_TEST(test_output_buffer_too_small);
    return UNITY_END();
}
```

`run-tests.ps1`의 `$tests`에 추가:
```powershell
    "test-web-bridge" = @("$PSScriptRoot\test-web-bridge.c", "$root\main\web-bridge.c")
```

- [ ] **Step 2: 실패 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-web-bridge`
Expected: `compile failed`

- [ ] **Step 3: 구현**

`firmware/main/web-bridge.h`:
```c
#pragma once
#include <stdbool.h>
#include <stddef.h>

// Translates an HTTP method + URL query into a protocol request line, allowing only read-only
// commands over GET and set_time over POST. Returns false for anything else or malformed input.
bool web_bridge_build_request(const char *method, const char *query, char *out, size_t out_len);
```

`firmware/main/web-bridge.c`:
```c
#include "web-bridge.h"
#include <stdio.h>
#include <string.h>

#define MAX_VALUE 24

static bool is_cmd_char(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool all(const char *s, bool (*ok)(char))
{
    if (*s == '\0') return false;
    for (; *s; s++) if (!ok(*s)) return false;
    return true;
}

static bool allowed(const char *method, const char *cmd)
{
    if (strcmp(method, "GET") == 0) {
        return strcmp(cmd, "ping") == 0 || strcmp(cmd, "read_now") == 0 ||
               strcmp(cmd, "get_status") == 0 || strcmp(cmd, "get_log") == 0;
    }
    if (strcmp(method, "POST") == 0) return strcmp(cmd, "set_time") == 0;
    return false;
}

bool web_bridge_build_request(const char *method, const char *query, char *out, size_t out_len)
{
    char cmd[MAX_VALUE] = "", offset[MAX_VALUE] = "", limit[MAX_VALUE] = "", epoch[MAX_VALUE] = "";
    const char *p = query;
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        if (!eq || eq > end) return false;
        size_t klen = (size_t)(eq - p), vlen = (size_t)(end - eq - 1);
        if (vlen == 0 || vlen >= MAX_VALUE) return false;
        char *dst;
        if (klen == 3 && strncmp(p, "cmd", 3) == 0) dst = cmd;
        else if (klen == 6 && strncmp(p, "offset", 6) == 0) dst = offset;
        else if (klen == 5 && strncmp(p, "limit", 5) == 0) dst = limit;
        else if (klen == 5 && strncmp(p, "epoch", 5) == 0) dst = epoch;
        else return false;
        memcpy(dst, eq + 1, vlen);
        dst[vlen] = '\0';
        p = amp ? amp + 1 : end;
    }
    if (!all(cmd, is_cmd_char) || !allowed(method, cmd)) return false;
    if (*offset && !all(offset, is_digit)) return false;
    if (*limit && !all(limit, is_digit)) return false;
    if (*epoch && !all(epoch, is_digit)) return false;

    int n = snprintf(out, out_len, "{\"cmd\":\"%s\"", cmd);
    if (n < 0 || (size_t)n >= out_len) return false;
    size_t used = (size_t)n;
    const char *keys[] = { "offset", "limit", "epoch" };
    const char *vals[] = { offset, limit, epoch };
    for (int i = 0; i < 3; i++) {
        if (!*vals[i]) continue;
        n = snprintf(out + used, out_len - used, ",\"%s\":%s", keys[i], vals[i]);
        if (n < 0 || (size_t)n >= out_len - used) return false;
        used += (size_t)n;
    }
    n = snprintf(out + used, out_len - used, "}");
    return n >= 0 && (size_t)n < out_len - used;
}
```

- [ ] **Step 4: 통과 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-web-bridge`
Expected: 4 Tests 0 Failures

- [ ] **Step 5: 커밋**

```bash
git add firmware/main/web-bridge.h firmware/main/web-bridge.c firmware/test_host/test-web-bridge.c firmware/test_host/run-tests.ps1
git commit -m "feat: HTTP 쿼리를 프로토콜 요청으로 변환하는 web-bridge 추가"
```

---

### Task 4: 상태 LED에 WiFi 접속 중 상태 추가

**Files:**
- Modify: `firmware/main/status-led-color.h`, `firmware/main/status-led-color.c`
- Test: `firmware/test_host/test-status-led.c`

**Interfaces:**
- Produces: `STATUS_LED_WIFI_CONNECTING` = 파랑(`{0,0,STATUS_LED_BRIGHTNESS}`) 0.5초 깜빡임.

- [ ] **Step 1: 실패하는 테스트**

`test-status-led.c`에 추가(+`RUN_TEST`):
```c
void test_wifi_connecting_blinks_blue(void) {
    assert_rgb(status_led_color(STATUS_LED_WIFI_CONNECTING, 0), 0, 0, STATUS_LED_BRIGHTNESS);
    assert_rgb(status_led_color(STATUS_LED_WIFI_CONNECTING, 500), 0, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_WIFI_CONNECTING, 1000), 0, 0, STATUS_LED_BRIGHTNESS);
}
```

- [ ] **Step 2: 실패 확인** — Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-status-led` → 컴파일 오류

- [ ] **Step 3: 구현**

`status-led-color.h` enum에 `STATUS_LED_ERROR` 뒤 추가:
```c
    STATUS_LED_WIFI_CONNECTING, // credentials set but not connected: blinking blue
```
`status-led-color.c`의 switch에 추가:
```c
    case STATUS_LED_WIFI_CONNECTING: {
        status_led_rgb_t blue = { 0, 0, STATUS_LED_BRIGHTNESS };
        return (now_ms / STATUS_LED_BLINK_HALF_MS) % 2 == 0 ? blue : off;
    }
```

- [ ] **Step 4: 통과 확인** — 5 Tests 0 Failures

- [ ] **Step 5: 커밋**

```bash
git add firmware/main/status-led-color.h firmware/main/status-led-color.c firmware/test_host/test-status-led.c
git commit -m "feat: 상태 LED에 WiFi 접속 중(파랑 깜빡임) 추가"
```

---

### Task 5: settings WiFi 키 + wifi 모듈(STA, 재접속, SNTP)

**Files:**
- Modify: `firmware/main/settings.h`, `firmware/main/settings.c`
- Create: `firmware/main/wifi.h`, `firmware/main/wifi.c`
- Modify: `firmware/main/CMakeLists.txt` (SRCS, PRIV_REQUIRES)

**Interfaces:**
- Consumes: `clock_mark_valid(CLOCK_SOURCE_NTP)`
- Produces:
  ```c
  // settings.h
  bool settings_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len);  // false if none stored
  esp_err_t settings_set_wifi_credentials(const char *ssid, const char *password);              // ssid "" → erase both keys
  // wifi.h
  typedef enum { WIFI_OFF, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED } wifi_state_t;
  esp_err_t wifi_init(void);                                            // netif/event/driver init; starts STA if credentials exist
  esp_err_t wifi_set_credentials(const char *ssid, const char *password);   // persists + (re)connects; "" → disconnect, WIFI_OFF
  wifi_state_t wifi_state(void);
  const char *wifi_state_name(void);                                    // "off"|"connecting"|"connected"|"failed"
  void wifi_ip(char *buf, size_t len);                                  // "" unless connected
  ```
  이 태스크는 IDF 빌드로만 검증(호스트 테스트 없음). app-main 연동은 Task 6.

- [ ] **Step 1: settings 확장**

`settings.h`에 `#include <stdbool.h>`, `#include <stddef.h>` 추가 후:
```c
bool settings_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len);
esp_err_t settings_set_wifi_credentials(const char *ssid, const char *password);
```
`settings.c`에 `#define KEY_WIFI_SSID "wifi_ssid"`, `#define KEY_WIFI_PASS "wifi_pass"` 추가 후:
```c
bool settings_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    size_t n = ssid_len;
    if (nvs_get_str(g_nvs, KEY_WIFI_SSID, ssid, &n) != ESP_OK || ssid[0] == '\0') return false;
    n = pass_len;
    if (nvs_get_str(g_nvs, KEY_WIFI_PASS, password, &n) != ESP_OK) password[0] = '\0';
    return true;
}

esp_err_t settings_set_wifi_credentials(const char *ssid, const char *password)
{
    esp_err_t err;
    if (ssid[0] == '\0') {
        err = nvs_erase_key(g_nvs, KEY_WIFI_SSID);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
        err = nvs_erase_key(g_nvs, KEY_WIFI_PASS);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    } else {
        err = nvs_set_str(g_nvs, KEY_WIFI_SSID, ssid);
        if (err != ESP_OK) return err;
        err = nvs_set_str(g_nvs, KEY_WIFI_PASS, password);
        if (err != ESP_OK) return err;
    }
    return nvs_commit(g_nvs);
}
```

- [ ] **Step 2: wifi 모듈**

`firmware/main/wifi.h`:
```c
#pragma once
#include <stddef.h>
#include "esp_err.h"

typedef enum { WIFI_OFF, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_FAILED } wifi_state_t;

esp_err_t wifi_init(void);
esp_err_t wifi_set_credentials(const char *ssid, const char *password);
wifi_state_t wifi_state(void);
const char *wifi_state_name(void);
void wifi_ip(char *buf, size_t len);
```

`firmware/main/wifi.c`:
```c
#include "wifi.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "clock.h"
#include "settings.h"

#define FAILED_AFTER_RETRIES 5
#define BACKOFF_MAX_S 60

static const char *TAG = "wifi";

static volatile wifi_state_t g_state = WIFI_OFF;
static char g_ip[16] = "";
static bool g_started;          // esp_wifi_start() has been called
static bool g_want_connect;     // credentials present; reconnect on disconnect
static int g_retries;
static esp_timer_handle_t g_retry_timer;
static bool g_sntp_started;

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    clock_mark_valid(CLOCK_SOURCE_NTP);
    ESP_LOGI(TAG, "time synced via SNTP");
}

static void start_sntp_once(void)
{
    if (g_sntp_started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sntp_sync;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) g_sntp_started = true;
    else ESP_LOGE(TAG, "SNTP init failed");
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (g_want_connect) esp_wifi_connect();
}

static void schedule_retry(void)
{
    int delay_s = 1 << (g_retries < 6 ? g_retries : 6);   // 1,2,4,8,16,32,64
    if (delay_s > BACKOFF_MAX_S) delay_s = BACKOFF_MAX_S;
    g_retries++;
    g_state = g_retries >= FAILED_AFTER_RETRIES ? WIFI_FAILED : WIFI_CONNECTING;
    ESP_LOGW(TAG, "disconnected, retry %d in %d s", g_retries, delay_s);
    esp_timer_start_once(g_retry_timer, (uint64_t)delay_s * 1000000);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_START) {
        if (g_want_connect) { g_state = WIFI_CONNECTING; esp_wifi_connect(); }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        g_ip[0] = '\0';
        if (g_want_connect) schedule_retry();
        else g_state = WIFI_OFF;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    snprintf(g_ip, sizeof g_ip, IPSTR, IP2STR(&ev->ip_info.ip));
    g_retries = 0;
    g_state = WIFI_CONNECTED;
    ESP_LOGI(TAG, "connected, ip=%s", g_ip);
    start_sntp_once();
}

static esp_err_t apply_config(const char *ssid, const char *password)
{
    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, ssid, sizeof cfg.sta.ssid);
    strncpy((char *)cfg.sta.password, password, sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

esp_err_t wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));
    esp_timer_create_args_t targs = { .callback = retry_timer_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &g_retry_timer));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    char ssid[33], password[64];
    if (!settings_wifi_credentials(ssid, sizeof ssid, password, sizeof password)) {
        ESP_LOGI(TAG, "no credentials stored");
        return ESP_OK;
    }
    err = apply_config(ssid, password);
    if (err != ESP_OK) return err;
    g_want_connect = true;
    g_state = WIFI_CONNECTING;
    err = esp_wifi_start();
    g_started = err == ESP_OK;
    return err;
}

esp_err_t wifi_set_credentials(const char *ssid, const char *password)
{
    esp_err_t err = settings_set_wifi_credentials(ssid, password);
    if (err != ESP_OK) return err;

    if (ssid[0] == '\0') {
        g_want_connect = false;
        g_retries = 0;
        esp_timer_stop(g_retry_timer);
        if (g_started) { esp_wifi_disconnect(); esp_wifi_stop(); g_started = false; }
        g_ip[0] = '\0';
        g_state = WIFI_OFF;
        return ESP_OK;
    }

    g_want_connect = false;                 // suppress the reconnect scheduled by the disconnect below
    esp_timer_stop(g_retry_timer);
    if (g_started) esp_wifi_disconnect();
    err = apply_config(ssid, password);
    if (err != ESP_OK) return err;
    g_retries = 0;
    g_want_connect = true;
    g_state = WIFI_CONNECTING;
    if (!g_started) {
        err = esp_wifi_start();             // STA_START event triggers esp_wifi_connect()
        g_started = err == ESP_OK;
        return err;
    }
    return esp_wifi_connect();
}

wifi_state_t wifi_state(void) { return g_state; }

const char *wifi_state_name(void)
{
    switch (g_state) {
    case WIFI_CONNECTING: return "connecting";
    case WIFI_CONNECTED: return "connected";
    case WIFI_FAILED: return "failed";
    default: return "off";
    }
}

void wifi_ip(char *buf, size_t len)
{
    if (g_state == WIFI_CONNECTED) snprintf(buf, len, "%s", g_ip);
    else if (len) buf[0] = '\0';
}
```

`firmware/main/CMakeLists.txt`: SRCS에 `"wifi.c" "web-bridge.c"` 추가, PRIV_REQUIRES에 `esp_wifi esp_netif esp_event` 추가.

- [ ] **Step 3: 빌드 확인**

Run (PowerShell):
```powershell
. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"; cd C:\work\s3-bme-280\firmware; idf.py build
```
Expected: `Project build complete.` — `wifi.c`는 아직 app-main에서 호출되지 않지만 컴파일·링크된다. 미사용 경고가 있으면 함수는 그대로 두고(다음 태스크에서 사용) 경고 원인만 고친다. 호스트 테스트도 통과 확인.

- [ ] **Step 4: 커밋**

```bash
git add firmware/main/settings.h firmware/main/settings.c firmware/main/wifi.h firmware/main/wifi.c firmware/main/CMakeLists.txt
git commit -m "feat: WiFi STA 접속·재접속·SNTP 모듈과 NVS 자격증명 저장 추가"
```

---

### Task 6: web 서버 + 내장 페이지 + app-main 연동(뮤텍스) + 플래시

**Files:**
- Create: `firmware/main/web.h`, `firmware/main/web.c`, `firmware/main/web/index.html`
- Modify: `firmware/main/app-main.c`, `firmware/main/CMakeLists.txt`, `firmware/sdkconfig.defaults`

**Interfaces:**
- Consumes: `web_bridge_build_request`, `protocol_handle_line`, `wifi_*`, `clock_time_source`, `settings_wifi_credentials`
- Produces:
  ```c
  typedef struct { void *ctx; void (*lock)(void *ctx); void (*unlock)(void *ctx); } web_lock_t;
  esp_err_t web_start(const web_lock_t *lock);   // port 80; GET /, GET|POST /api
  ```

- [ ] **Step 1: web 모듈**

`firmware/main/web.h`:
```c
#pragma once
#include "esp_err.h"

// Serializes device access with the main loop; app_main supplies mutex lock/unlock.
typedef struct {
    void *ctx;
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} web_lock_t;

esp_err_t web_start(const web_lock_t *lock);
```

`firmware/main/web.c`:
```c
#include "web.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"

#include "protocol.h"
#include "web-bridge.h"

static const char *TAG = "web";
static web_lock_t g_lock;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);   // EMBED_TXTFILES appends a NUL
}

static void chunk_write(void *ctx, const char *data, size_t len)
{
    httpd_resp_send_chunk((httpd_req_t *)ctx, data, (ssize_t)len);
}

static esp_err_t api_handler(httpd_req_t *req)
{
    char query[128] = "";
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen >= sizeof query) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query too long");
    if (qlen > 0) httpd_req_get_url_query_str(req, query, sizeof query);

    const char *method = req->method == HTTP_POST ? "POST" : "GET";
    char line[PROTOCOL_MAX_LINE];
    httpd_resp_set_type(req, "application/json");
    if (!web_bridge_build_request(method, query, line, sizeof line)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"forbidden\"}\n", HTTPD_RESP_USE_STRLEN);
    }
    g_lock.lock(g_lock.ctx);
    protocol_handle_line(line, chunk_write, req);
    g_lock.unlock(g_lock.ctx);
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t web_start(const web_lock_t *lock)
{
    g_lock = *lock;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) return err;

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t api_get = { .uri = "/api", .method = HTTP_GET, .handler = api_handler };
    httpd_uri_t api_post = { .uri = "/api", .method = HTTP_POST, .handler = api_handler };
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &api_get);
    httpd_register_uri_handler(server, &api_post);
    ESP_LOGI(TAG, "http server started");
    return ESP_OK;
}
```

- [ ] **Step 2: 내장 페이지**

`firmware/main/web/index.html` (한국어 UI, 외부 리소스 없음):
```html
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BME280 로거</title>
<style>
  body { font-family: system-ui, sans-serif; margin: 16px; background: #f5f5f5; color: #222; }
  h1 { font-size: 1.3rem; margin: 0 0 12px; }
  .cards { display: flex; gap: 12px; flex-wrap: wrap; }
  .card { background: #fff; border-radius: 8px; padding: 12px 16px; min-width: 140px; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  .card .label { font-size: .8rem; color: #666; }
  .card .value { font-size: 1.8rem; font-weight: 600; }
  .status { margin: 12px 0; font-size: .9rem; color: #444; line-height: 1.6; }
  .range button { margin-right: 6px; padding: 6px 12px; }
  .range button.active { background: #333; color: #fff; }
  canvas { width: 100%; height: 160px; background: #fff; border-radius: 8px; display: block; margin: 8px 0; }
  #timebox { background: #fff7e0; border: 1px solid #e8c866; border-radius: 8px; padding: 12px; margin-top: 12px; }
  #timebox input, #timebox button { padding: 6px 10px; margin: 4px 4px 4px 0; }
  .hidden { display: none; }
  #msg { color: #a00; font-size: .85rem; }
</style>
</head>
<body>
<h1>BME280 로거</h1>
<div class="cards">
  <div class="card"><div class="label">온도</div><div class="value"><span id="temp">--</span> °C</div></div>
  <div class="card"><div class="label">습도</div><div class="value"><span id="hum">--</span> %</div></div>
  <div class="card"><div class="label">기압</div><div class="value"><span id="press">--</span> hPa</div></div>
</div>
<div class="status" id="status">상태를 불러오는 중…</div>

<div id="timebox" class="hidden">
  <div>장치 시각이 NTP로 동기화되지 않았습니다. 수동으로 설정할 수 있습니다.</div>
  <button id="btn-browser-time">이 컴퓨터 시각으로 설정</button>
  <div>
    <input type="datetime-local" id="manual-time" step="1">
    <button id="btn-manual-time">입력한 시각으로 설정</button>
  </div>
  <div id="msg"></div>
</div>

<div class="range">
  <button data-hours="1" class="active">1시간</button>
  <button data-hours="24">1일</button>
  <button data-hours="168">1주</button>
  <span id="loading"></span>
</div>
<canvas id="chart-temp"></canvas>
<canvas id="chart-hum"></canvas>
<canvas id="chart-press"></canvas>

<script>
(function () {
  const PAGE = 500;
  let rangeHours = 1;
  let intervalS = 60;

  async function api(cmd, params, method) {
    const q = new URLSearchParams(Object.assign({ cmd }, params || {}));
    const res = await fetch('/api?' + q.toString(), { method: method || 'GET' });
    const body = await res.json();
    if (!body.ok) throw new Error(body.error || 'error');
    return body;
  }

  function fmt(n, d) { return Number(n).toFixed(d); }
  function pad(n) { return String(n).padStart(2, '0'); }
  function hms(s) { return pad(Math.floor(s / 3600)) + ':' + pad(Math.floor(s % 3600 / 60)) + ':' + pad(s % 60); }
  function fmtTime(d) { return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes()); }

  async function refreshNow() {
    try {
      const r = await api('read_now');
      document.getElementById('temp').textContent = fmt(r.temp_c, 2);
      document.getElementById('hum').textContent = fmt(r.hum_pct, 2);
      document.getElementById('press').textContent = fmt(r.pressure_pa / 100, 2);
    } catch (e) {
      document.getElementById('temp').textContent = '--';
    }
    try {
      const s = await api('get_status');
      intervalS = s.interval_s;
      const wifi = { off: '미설정', connecting: '접속 중', connected: '연결됨', failed: '접속 실패' }[s.wifi_state] || s.wifi_state;
      const src = { none: '없음', pc: 'PC', ntp: 'NTP' }[s.time_source] || s.time_source;
      document.getElementById('status').innerHTML =
        '저장 레코드 ' + s.count + ' / ' + s.capacity + ' · 측정 주기 ' + s.interval_s + '초 · ' +
        '센서 ' + (s.sensor_ok ? '정상' : '오류') + ' · 저장소 ' + (s.store_ok ? '정상' : '오류') + '<br>' +
        '시간 출처 ' + src + ' · WiFi ' + wifi + (s.ip ? ' (' + s.ip + ')' : '') + ' · 부팅 #' + s.boot_id + ' · 가동 ' + hms(s.uptime_s);
      document.getElementById('timebox').classList.toggle('hidden', s.time_source === 'ntp');
    } catch (e) {
      document.getElementById('status').textContent = '상태 조회 실패: ' + e.message;
    }
  }

  async function setTime(epoch) {
    const msg = document.getElementById('msg');
    try {
      await api('set_time', { epoch }, 'POST');
      msg.textContent = '';
      await refreshNow();
      await loadChart();
    } catch (e) {
      msg.textContent = '시간 설정 실패: ' + e.message;
    }
  }
  document.getElementById('btn-browser-time').onclick = () => setTime(Math.floor(Date.now() / 1000));
  document.getElementById('btn-manual-time').onclick = () => {
    const v = document.getElementById('manual-time').value;
    if (!v) { document.getElementById('msg').textContent = '날짜와 시각을 입력하세요.'; return; }
    setTime(Math.floor(new Date(v).getTime() / 1000));   // datetime-local is interpreted in the browser's local zone
  };

  async function loadChart() {
    const loading = document.getElementById('loading');
    loading.textContent = '불러오는 중…';
    try {
      const st = await api('get_status');
      const need = Math.ceil(rangeHours * 3600 / Math.max(st.interval_s, 1));
      let offset = Math.max(0, st.count - need);
      const total = st.count;
      const recs = [];
      while (offset < total) {
        const page = await api('get_log', { offset, limit: PAGE });
        for (const r of page.records) if (r[4] & 1) recs.push({ t: r[0], temp: r[1] / 100, hum: r[2] / 100, press: r[3] / 100 });
        offset += PAGE;
      }
      const since = Date.now() / 1000 - rangeHours * 3600;
      const data = recs.filter(r => r.t >= since);
      draw('chart-temp', data, 'temp', '온도 (°C)', 2);
      draw('chart-hum', data, 'hum', '습도 (%)', 1);
      draw('chart-press', data, 'press', '기압 (hPa)', 2);
      loading.textContent = data.length ? '' : '표시할 데이터가 없습니다';
    } catch (e) {
      loading.textContent = '이력 조회 실패: ' + e.message;
    }
  }

  function draw(id, data, key, title, digits) {
    const c = document.getElementById(id);
    const W = c.width = c.clientWidth * devicePixelRatio, H = c.height = c.clientHeight * devicePixelRatio;
    const ctx = c.getContext('2d');
    ctx.clearRect(0, 0, W, H);
    ctx.font = (12 * devicePixelRatio) + 'px sans-serif';
    ctx.fillStyle = '#444';
    ctx.fillText(title, 8 * devicePixelRatio, 16 * devicePixelRatio);
    if (data.length < 2) return;
    const padL = 56 * devicePixelRatio, padR = 8 * devicePixelRatio, padT = 24 * devicePixelRatio, padB = 20 * devicePixelRatio;
    // downsample to one point per pixel column by averaging
    const cols = Math.max(1, Math.floor((W - padL - padR) / devicePixelRatio));
    const per = Math.max(1, Math.ceil(data.length / cols));
    const pts = [];
    for (let i = 0; i < data.length; i += per) {
      const slice = data.slice(i, i + per);
      pts.push({ t: slice[0].t, v: slice.reduce((a, r) => a + r[key], 0) / slice.length });
    }
    let min = Math.min(...pts.map(p => p.v)), max = Math.max(...pts.map(p => p.v));
    if (max - min < 0.1) { min -= 0.5; max += 0.5; }
    const t0 = pts[0].t, t1 = pts[pts.length - 1].t || t0 + 1;
    const x = t => padL + (t - t0) / Math.max(t1 - t0, 1) * (W - padL - padR);
    const y = v => padT + (max - v) / (max - min) * (H - padT - padB);
    ctx.strokeStyle = '#ddd'; ctx.lineWidth = devicePixelRatio;
    for (let g = 0; g <= 4; g++) { const yy = padT + g * (H - padT - padB) / 4; ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(W - padR, yy); ctx.stroke();
      ctx.fillText(fmt(max - g * (max - min) / 4, digits), 4 * devicePixelRatio, yy + 4 * devicePixelRatio); }
    ctx.fillText(fmtTime(new Date(t0 * 1000)), padL, H - 4 * devicePixelRatio);
    const endLabel = fmtTime(new Date(t1 * 1000));
    ctx.fillText(endLabel, W - padR - ctx.measureText(endLabel).width, H - 4 * devicePixelRatio);
    ctx.strokeStyle = '#2a6ebb'; ctx.lineWidth = 1.5 * devicePixelRatio;
    ctx.beginPath();
    pts.forEach((p, i) => i ? ctx.lineTo(x(p.t), y(p.v)) : ctx.moveTo(x(p.t), y(p.v)));
    ctx.stroke();
  }

  document.querySelectorAll('.range button').forEach(b => b.onclick = () => {
    document.querySelectorAll('.range button').forEach(o => o.classList.remove('active'));
    b.classList.add('active');
    rangeHours = Number(b.dataset.hours);
    loadChart();
  });

  refreshNow();
  loadChart();
  setInterval(refreshNow, 10000);
  setInterval(loadChart, 60000);
})();
</script>
</body>
</html>
```

- [ ] **Step 3: app-main 연동**

`firmware/main/CMakeLists.txt`: SRCS에 `"web.c"` 추가, `EMBED_TXTFILES "web/index.html"` 추가, PRIV_REQUIRES에 `esp_http_server` 추가. 최종:
```cmake
idf_component_register(
    SRCS "app-main.c" "spi-bus.c" "clock.c" "settings.c" "sampler.c" "protocol.c" "json-mini.c" "status-led.c" "status-led-color.c"
         "wifi.c" "web-bridge.c" "web.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES log_store bme280 esp_driver_spi esp_driver_usb_serial_jtag nvs_flash esp_partition esp_timer esp_driver_rmt
                  esp_wifi esp_netif esp_event esp_http_server)
```

`firmware/sdkconfig.defaults`에 추가:
```
CONFIG_HTTPD_MAX_URI_LEN=512
```

`firmware/main/app-main.c` 수정:
- include 추가: `#include "freertos/semphr.h"`, `#include "web.h"`, `#include "wifi.h"`.
- 전역 추가: `static SemaphoreHandle_t g_device_lock;`
- ops 추가:
```c
static int op_set_wifi(void *c, const char *ssid, const char *password)
{
    (void)c;
    return wifi_set_credentials(ssid, password) == ESP_OK ? 0 : -2;
}
static const char *op_wifi_state(void *c) { (void)c; return wifi_state_name(); }
static void op_wifi_ip(void *c, char *buf, size_t len) { (void)c; wifi_ip(buf, len); }
static const char *op_time_source(void *c) { (void)c; return clock_time_source(); }
```
  `g_ops` 초기화에 `.set_wifi = op_set_wifi, .wifi_state = op_wifi_state, .wifi_ip = op_wifi_ip, .time_source = op_time_source` 추가.
- 잠금 콜백:
```c
static void device_lock(void *c) { (void)c; xSemaphoreTake(g_device_lock, portMAX_DELAY); }
static void device_unlock(void *c) { (void)c; xSemaphoreGive(g_device_lock); }
```
- `app_main`: `status_led_init` 다음에
```c
    g_device_lock = xSemaphoreCreateMutex();
    err = wifi_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));
    web_lock_t lock = { .ctx = NULL, .lock = device_lock, .unlock = device_unlock };
    err = web_start(&lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "web start failed: %s", esp_err_to_name(err));
```
- 메인 루프: `protocol_handle_line(line, usb_write, NULL);`를 `device_lock(NULL); protocol_handle_line(line, usb_write, NULL); device_unlock(NULL);`로, `sampler_tick(...)`도 `device_lock(NULL); sampler_tick(...); device_unlock(NULL);`로 감싼다.
- LED 결정을 다음으로 교체:
```c
        status_led_state_t led = STATUS_LED_ERROR;
        if (sampler_sensor_ok() && sampler_store_ok()) {
            wifi_state_t ws = wifi_state();
            if (ws == WIFI_CONNECTING || ws == WIFI_FAILED) led = STATUS_LED_WIFI_CONNECTING;
            else led = clock_time_valid() ? STATUS_LED_OK : STATUS_LED_OK_NO_TIME;
        }
        status_led_update(led, (uint32_t)(esp_timer_get_time() / 1000));
```

- [ ] **Step 4: 빌드·플래시·기본 확인**

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"; cd C:\work\s3-bme-280\firmware; idf.py build; idf.py -p COM9 flash
```
Expected: 빌드 완료(바이너리 크기가 앱 파티션 2 MB 안), 플래시 성공. 이어서 시리얼로 확인(Go CLI는 Task 7 전이라 `status`에 WiFi 줄이 없지만 프로토콜은 확인 가능):
```powershell
cd C:\work\s3-bme-280\pc; .\bme280-tool.exe status
```
Expected: 정상 응답(`Sensor: ok`), 부팅 세대 +1. 아직 WiFi 미설정 → LED는 녹색(깜빡임→고정).

- [ ] **Step 5: 커밋**

```bash
git add firmware
git commit -m "feat: 내장 웹 서버·페이지 추가 및 뮤텍스로 장치 접근 직렬화"
```

---

### Task 7: Go CLI `wifi` 명령 + status WiFi/시간 출처 줄

**Files:**
- Modify: `pc/internal/client/client.go`, `pc/internal/client/client_test.go`
- Modify: `pc/internal/cli/cli.go`, `pc/internal/cli/cli_test.go`

**Interfaces:**
- Produces: `func (c *DeviceClient) SetWifi(ssid, password string) (map[string]any, error)` → `set_wifi {ssid, password}`; CLI `wifi <SSID> <PASSWORD>` / `wifi --clear`; `status`에 `WiFi: …`, `Time source: …` 줄(필드가 응답에 있을 때만).

- [ ] **Step 1: 실패하는 테스트**

`client_test.go`에 추가:
```go
func TestSetWifiSendsUTF8AndEscapes(t *testing.T) {
	f := transport.NewFake(`{"ok":true}`)
	if _, err := New(f).SetWifi("우리집", `p"w\1234567`); err != nil {
		t.Fatal(err)
	}
	line := f.Sent[0]
	if !strings.Contains(line, `"ssid":"우리집"`) || !strings.Contains(line, `"password":"p\"w\\1234567"`) {
		t.Fatalf("sent %q", line)
	}
	if got := sentJSON(t, line); got["cmd"] != "set_wifi" {
		t.Fatalf("sent %v", got)
	}
}
```
`cli_test.go`: `status` 상수를 새 필드 포함으로 교체
```go
	status  = `{"ok":true,"count":1234,"capacity":32512,"interval_s":60,"sensor_ok":true,"store_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121,"wifi_state":"connected","ip":"192.168.0.23","time_source":"ntp"}`
```
그리고 추가:
```go
func TestStatusPrintsWifiAndTimeSource(t *testing.T) {
	factory, _ := factoryWith(t, status)
	_, out, _ := run(t, factory, "", "--port", "COM9", "status")
	if !strings.Contains(out, "WiFi: connected (192.168.0.23)") || !strings.Contains(out, "Time source: ntp") {
		t.Fatalf("out %q", out)
	}
	old := strings.Replace(status, `,"wifi_state":"connected","ip":"192.168.0.23","time_source":"ntp"`, "", 1)
	factory, _ = factoryWith(t, old)
	_, out, _ = run(t, factory, "", "--port", "COM9", "status")
	if strings.Contains(out, "WiFi:") {
		t.Fatalf("old firmware must not print WiFi line: %q", out)
	}
}

func TestWifiSetAndClear(t *testing.T) {
	factory, f := factoryWith(t, `{"ok":true}`)
	code, out, _ := run(t, factory, "", "--port", "COM9", "wifi", "우리집", "pw12345678")
	if code != 0 || !strings.Contains(out, "WiFi credentials saved") {
		t.Fatalf("code %d out %q", code, out)
	}
	if got := sent(t, f, 2); got["cmd"] != "set_wifi" || got["ssid"] != "우리집" || got["password"] != "pw12345678" {
		t.Fatalf("sent %v", got)
	}
	factory, f = factoryWith(t, `{"ok":true}`)
	code, out, _ = run(t, factory, "", "--port", "COM9", "wifi", "--clear")
	if code != 0 || !strings.Contains(out, "WiFi credentials cleared") || sent(t, f, 2)["ssid"] != "" {
		t.Fatalf("code %d out %q sent %v", code, out, f.Sent)
	}
}

func TestWifiValidatesBeforeOpen(t *testing.T) {
	f := transport.NewFake()
	f.OpenErr = errors.New("no device")
	factory := func(string) transport.Transport { return f }
	for _, args := range [][]string{
		{"wifi"},                                            // missing args
		{"wifi", "home"},                                    // missing password
		{"wifi", "home", "short"},                           // password < 8
		{"wifi", strings.Repeat("a", 33), "pw12345678"},     // ssid > 32 bytes
		{"wifi", "home", strings.Repeat("p", 64)},           // password > 63
		{"wifi", "", "pw12345678"},                          // empty ssid
	} {
		code, _, e := run(t, factory, "", append([]string{"--port", "COM9"}, args...)...)
		if code != 2 || !strings.Contains(e, "Error:") || f.Opened {
			t.Fatalf("%v: code %d stderr %q opened %v", args, code, e, f.Opened)
		}
	}
}
```
(`errors` import 추가.) `wifi home ""`(빈 비밀번호, 개방망)는 허용된다 — 별도 테스트는 두지 않는다.

- [ ] **Step 2: 실패 확인** — `cd pc; go test ./...` → 컴파일 오류(`SetWifi` 없음)

- [ ] **Step 3: 구현**

`client.go`에 추가:
```go
func (c *DeviceClient) SetWifi(ssid, password string) (map[string]any, error) {
	return c.Request("set_wifi", defaultTimeout, map[string]any{"ssid": ssid, "password": password})
}
```
(`encoding/json`은 비ASCII를 그대로 두고 `"`·`\`만 이스케이프한다. `<`, `>`, `&`는 `\u003c` 등으로 이스케이프되므로 비밀번호에 이 문자가 있으면 장치 파서가 거부한다 — `Request`에서 `json.NewEncoder` + `SetEscapeHTML(false)`로 바꿔 이를 막는다:)
```go
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(req); err != nil {
		return nil, err
	}
	if err := c.t.SendLine(strings.TrimRight(buf.String(), "\n")); err != nil {
```
(기존 `json.Marshal` 부분을 위로 교체, `bytes` import.)

`cli.go`:
- usage 블록에 `  wifi <SSID> <PASSWORD> | --clear     save WiFi credentials (empty password = open network)` 추가.
- `commands`에 `"wifi": cmdWifi` 등록하고 `prepare`에서 `parseWifiArgs` 사용:
```go
type wifiParams struct {
	clear    bool
	ssid     string
	password string
}

func parseWifiArgs(args []string) (wifiParams, error) {
	if len(args) == 1 && args[0] == "--clear" {
		return wifiParams{clear: true}, nil
	}
	if len(args) != 2 {
		return wifiParams{}, fmt.Errorf("usage: wifi <SSID> <PASSWORD> | wifi --clear")
	}
	ssid, password := args[0], args[1]
	if n := len(ssid); n < 1 || n > 32 {
		return wifiParams{}, fmt.Errorf("SSID must be 1-32 bytes (got %d)", n)
	}
	if n := len(password); n != 0 && (n < 8 || n > 63) {
		return wifiParams{}, fmt.Errorf("password must be empty or 8-63 bytes (got %d)", n)
	}
	return wifiParams{ssid: ssid, password: password}, nil
}

func cmdWifi(s *session, p wifiParams) (int, error) {
	if p.clear {
		if _, err := s.client.SetWifi("", ""); err != nil {
			return 1, err
		}
		fmt.Fprintln(s.stdout, "WiFi credentials cleared.")
		return 0, nil
	}
	if _, err := s.client.SetWifi(p.ssid, p.password); err != nil {
		return 1, err
	}
	fmt.Fprintln(s.stdout, "WiFi credentials saved. Run 'status' to check the connection.")
	return 0, nil
}
```
(현재 `cli.go`의 parse/run 분리 구조에 맞춰 연결한다: `prepare`가 `wifiParams`를 만들고 핸들러 클로저가 `cmdWifi`를 호출.)
- `cmdStatus`의 `Boot #` 줄 뒤에:
```go
	if ws, ok := st["wifi_state"].(string); ok {
		ip, _ := st["ip"].(string)
		if ws == "connected" && ip != "" {
			fmt.Fprintf(s.stdout, "WiFi: %s (%s)\n", ws, ip)
		} else {
			fmt.Fprintf(s.stdout, "WiFi: %s\n", ws)
		}
	}
	if src, ok := st["time_source"].(string); ok {
		fmt.Fprintf(s.stdout, "Time source: %s\n", src)
	}
```

- [ ] **Step 4: 통과 확인** — `cd pc; go test ./...; go vet ./...; go build -o bme280-tool.exe ./cmd/bme280-tool`

- [ ] **Step 5: 커밋**

```bash
git add pc
git commit -m "feat: bme280-tool wifi 명령과 status WiFi·시간 출처 표시 추가"
```

---

### Task 8: 실기 통합 확인 + 문서

**Files:**
- Modify: `README.md`, `docs/protocol.md`

- [ ] **Step 1: 실기 확인 (사용자 WiFi 정보 필요)**

컨트롤러가 사용자에게 SSID/비밀번호를 받아 실행한다(에이전트가 비밀번호를 추측하지 않는다). 순서:
```powershell
cd C:\work\s3-bme-280\pc
.\bme280-tool.exe wifi "<SSID>" "<PASSWORD>"
Start-Sleep 10
.\bme280-tool.exe status        # WiFi: connected (192.168.x.y), Time source: ntp (NTP는 수 초 걸릴 수 있음)
```
브라우저에서 `http://192.168.x.y/` 접속 → 현재값 카드, 상태 줄, 1시간 그래프 표시 확인. `curl http://192.168.x.y/api?cmd=get_status`로 JSON 확인, `curl -X POST "http://192.168.x.y/api?cmd=clear_log"` → 403 확인.
수동 시간 모드 확인: `.\bme280-tool.exe wifi --clear` 후 보드 리셋(USB 재연결) → 다시 `wifi` 설정 전에는 웹이 없으므로, 대신 NTP 차단이 어려우면 **시간 설정 섹션은 `time_source != ntp`일 때 표시되는 로직을 브라우저 개발자 도구에서 `document.getElementById('timebox').classList.remove('hidden')`로 노출**해 두 버튼이 `POST /api?cmd=set_time&epoch=…`를 보내고 `{"ok":true}`를 받는지 네트워크 탭으로 확인한다(장치는 NTP 우선이라 값은 무시되지만 경로 검증은 된다).
LED: `wifi` 설정 직후 파랑 깜빡임 → 연결되면 녹색. 공유기를 끄거나 비밀번호를 틀리게 설정(`wifi "<SSID>" wrongpass1`)하면 약 30초 후 `status`가 `failed`, LED 파랑 깜빡임 유지. 마지막에 올바른 자격증명으로 복구.

- [ ] **Step 2: 문서**

`README.md`:
- 상태 LED 표에 `| 파랑 깜빡임 | WiFi 자격증명은 있으나 접속 중/실패 |` 추가.
- `## PC 도구 (Go)` 사용법에 `.\bme280-tool.exe wifi <SSID> <PASSWORD>` / `wifi --clear` 추가.
- 새 절 `## 웹 조회` 추가:
```markdown
## 웹 조회

`bme280-tool wifi <SSID> <PASSWORD>`로 공유기 정보를 저장하면 장치가 접속하고 `status`에 IP가 표시된다. 브라우저에서 `http://<IP>/`를 열면 현재값·장치 상태·이력 그래프(1시간/1일/1주)를 볼 수 있다. WiFi가 연결되면 NTP로 시각이 자동 동기화되며, NTP 동기화 전에는 페이지에서 브라우저 시각 또는 직접 입력한 시각으로 설정할 수 있다. 웹은 조회 전용이며 삭제·주기 변경·WiFi 설정은 USB CLI로만 한다. `GET /api?cmd=…`는 시리얼 프로토콜과 같은 JSON을 돌려준다(읽기 명령만, `set_time`은 POST).
```

`docs/protocol.md`:
- §3에 `set_wifi` 항목 추가(요청/응답/오류: `bad_request` ssid 없음, `out_of_range` 길이, `store_error`), `get_status` 예시와 필드표에 `wifi_state`, `ip`, `time_source` 추가, `set_time`에 "NTP 동기 후에는 무시됨" 문장 추가.
- §2 프레이밍의 "이스케이프 지원하지 않는다"를 "`\"`·`\\`만 지원"으로 수정.
- 새 §9 `HTTP 브리지`: `GET /`, `GET /api?cmd=ping|read_now|get_status|get_log&offset&limit`, `POST /api?cmd=set_time&epoch`, 403 응답, 응답 본문은 시리얼 응답과 동일.

- [ ] **Step 3: 커밋**

```bash
git add README.md docs/protocol.md
git commit -m "docs: WiFi 설정·웹 조회·HTTP 브리지 사용법 추가"
```
