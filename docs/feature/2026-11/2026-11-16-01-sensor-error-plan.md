# 센서 실패 원인 노출 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## 원 요청

> 지금 장치가 COM9 에 연결되어 있는데, 빨간불이 들어와요.

(진단 결과 `sensor_ok:false`였고, 원인은 UART0 로그로만 나가 COM9에서 볼 수 없었다. "실패 원인을 `get_status`에 노출하도록 펌웨어를 수정할 수 있다"는 제안에 대한 답:)

> 네 진행

(표시 범위 질문에 대한 답:)

> 콘솔에서 그리고 웹에서

(구현 방식 A/B/C 중:)

> A로 진행

**Goal:** `get_status`에 `sensor_error`·`sensor_chip_id`를 추가하고, `bme280-tool status`와 웹 페이지에서 센서 실패 시 원인을 표시한다.

**Architecture:** 드라이버(`bme280_t`)가 마지막으로 읽은 chip ID를 기억하고, 샘플러가 초기화·주기 측정의 결과 코드를 기억한다. app-main이 SPI 초기화 실패 여부와 함께 두 값을 프로토콜 ops로 넘기고, 프로토콜이 JSON에 싣는다. CLI(Go)와 웹(JS)은 그 필드를 읽어 표시한다.

**Tech Stack:** ESP-IDF v6.0.1 C, 호스트 테스트 MinGW gcc + Unity, Go(pc 도구), 바닐라 JS(웹 페이지)

**Spec:** `docs/feature/2026-11/2026-11-16-01-sensor-error-design.md`

## Global Constraints

- JSON 키: `sensor_error`, `sensor_chip_id`. `get_status` 응답에서 `store_ok` 바로 뒤에 이 순서로 둔다.
- `sensor_error` 값: `none` / `spi_init` / `bus` / `chip_id` / `timeout`
- `sensor_chip_id`: 0~255 정수, 한 번도 못 읽었으면 `null`
- 정상(`sensor_ok:true`)일 때 CLI와 웹 표시는 지금과 똑같다. 필드가 없는 구버전 펌웨어 응답도 지금처럼 표시한다.
- CLI 출력은 영어, 웹 문구는 한국어. CLI는 기존 `ok`/`error` 라벨을 유지한다.
- 기대 chip ID는 `0x60`(`BME280_CHIP_ID`)
- `read_now` 오류 응답은 바꾸지 않는다(`sensor_error`). 자동 재초기화는 하지 않는다.
- 커밋 메시지는 `<type>: <한국어 제목>` 형식이고, `Co-Authored-By` 줄을 넣지 않는다.

## Review Focus

1. **chip ID가 0x00인 경우**: 웹은 `0x00`을 표시해야 한다. JS에서 `0`을 거짓으로 보고 생략하면 안 된다(Task 5에서 `!= null` 비교로 고정).
2. **SPI 초기화 실패**: `g_sensor`는 0으로 초기화된 전역이라 `bme280_init`이 한 번도 안 불리면 `last_chip_id`가 0이다. app-main이 SPI 실패 시 -1을 넘겨 `null`이 나와야 한다(Task 3, 호스트 테스트 불가 → 리뷰에서 확인).
3. **실패 후 복구**: 주기 측정이 실패했다가 다시 성공하면 `sensor_error`가 `none`으로 돌아와야 한다(Task 2 테스트).
4. **구버전 펌웨어 응답**: 필드가 없으면 CLI는 `Sensor: error`만, 웹은 `센서 오류`만 표시한다(Task 4 테스트, Task 5 코드).
5. **알 수 없는 `sensor_error` 값**(향후 펌웨어): CLI는 `error (<값>)`, 웹은 `센서 오류 (<값>)`처럼 원문을 그대로 보여준다(Task 4 테스트).

---

### Task 1: 드라이버가 마지막 chip ID를 기억

**Files:**
- Modify: `firmware/components/bme280/include/bme280.h` (`bme280_t`)
- Modify: `firmware/components/bme280/bme280.c` (`bme280_init`, `bme280_read`)
- Test: `firmware/test_host/test-bme280.c`

**Interfaces:**
- Produces: `bme280_t.last_chip_id` (`int16_t`). `bme280_init` 시작 시 -1로 두고, chip ID 레지스터 읽기에 성공할 때마다(init·read 모두) 읽은 값으로 갱신한다. 일치 여부와 상관없이 갱신한다.

- [ ] **Step 1: 실패하는 테스트 작성** (`main`의 `RUN_TEST` 목록에도 추가)

```c
void test_init_records_mismatched_chip_id(void) {
    fake_bus_regs[BME280_REG_CHIP_ID] = 0x58;
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, bme280_init(&dev, &fake_bus));
    TEST_ASSERT_EQUAL_INT16(0x58, dev.last_chip_id);
}

void test_init_bus_error_leaves_chip_id_unread(void) {
    dev.last_chip_id = 0x60;   // stale value from an earlier init must not survive
    fake_bus_fail = true;
    TEST_ASSERT_EQUAL(BME280_ERR_BUS, bme280_init(&dev, &fake_bus));
    TEST_ASSERT_EQUAL_INT16(-1, dev.last_chip_id);
}

void test_read_records_chip_id_after_disconnect(void) {
    load_example_calib();
    TEST_ASSERT_EQUAL(BME280_OK, bme280_init(&dev, &fake_bus));
    TEST_ASSERT_EQUAL_INT16(0x60, dev.last_chip_id);
    fake_bus_regs[BME280_REG_CHIP_ID] = 0xFF;
    bme280_reading_t r;
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, bme280_read(&dev, &r));
    TEST_ASSERT_EQUAL_INT16(0xFF, dev.last_chip_id);
}
```

- [ ] **Step 2: 실패 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-bme280`
Expected: 컴파일 실패(`bme280_t has no member named 'last_chip_id'`)

- [ ] **Step 3: 구현.** `bme280.h`의 `bme280_t`에 `int16_t last_chip_id;   // -1 until a chip id read succeeds`를 추가하고, `bme280.c`의 두 함수에서 chip ID를 읽은 직후 저장한다.

- [ ] **Step 4: 통과 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1`
Expected: `ALL PASSED` (test-sampler 포함 전체)

- [ ] **Step 5: 커밋**

```bash
git add firmware/components/bme280 firmware/test_host/test-bme280.c
git commit -m "feat: BME280 드라이버가 마지막으로 읽은 chip ID를 기억"
```

### Task 2: 샘플러가 센서 결과 코드를 기억

**Files:**
- Modify: `firmware/main/sampler.h`, `firmware/main/sampler.c`
- Modify: `firmware/main/app-main.c` (`sampler_init` 호출만, 펌웨어 빌드가 깨지지 않도록)
- Test: `firmware/test_host/test-sampler.c`

**Interfaces:**
- Produces:
  - `void sampler_init(bme280_t *sensor, log_store_t *store, uint32_t interval_s, int sensor_init_rc);` `sensor`는 `sensor_init_rc == BME280_OK`일 때만 NULL이 아니다.
  - `int sampler_sensor_rc(void);` 첫 주기 측정 전에는 `sensor_init_rc`를 반환하고, `sensor`가 NULL이면 계속 `sensor_init_rc`를 반환한다. 그 외에는 마지막 `bme280_read` 결과(`BME280_OK` / `BME280_ERR_*`)를 반환한다.
  - `sampler_sensor_ok()`의 동작은 바뀌지 않는다.

- [ ] **Step 1: 실패하는 테스트 작성.** 기존 호출 `sampler_init(&sensor, &store, 60)`은 `sampler_init(&sensor, &store, 60, BME280_OK)`로 바꾼다. 아래 테스트를 추가하고 `RUN_TEST`에 등록한다.

```c
void test_sensor_rc_tracks_failure_and_recovery(void) {
    TEST_ASSERT_EQUAL(BME280_OK, sampler_sensor_rc());
    fake_bus_regs[BME280_REG_CHIP_ID] = 0xFF;
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, sampler_sensor_rc());
    fake_bus_regs[BME280_REG_CHIP_ID] = BME280_CHIP_ID;
    sampler_tick(60, 60, false, 1);
    TEST_ASSERT_EQUAL(BME280_OK, sampler_sensor_rc());
}

void test_sensor_rc_keeps_init_failure_without_sensor(void) {
    sampler_init(NULL, &store, 60, BME280_ERR_CHIP_ID);
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, sampler_sensor_rc());
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, sampler_sensor_rc());
    TEST_ASSERT_FALSE(sampler_sensor_ok());
}
```

기존 `test_no_sensor`의 호출은 `sampler_init(NULL, &store, 60, BME280_ERR_BUS)`로 바꾼다.

- [ ] **Step 2: 실패 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-sampler`
Expected: 컴파일 실패(`too many arguments to function 'sampler_init'` 또는 `sampler_sensor_rc` 미정의)

- [ ] **Step 3: 구현.** `sampler.c`에 `static int g_sensor_rc`를 두고 `sampler_init`과 `sampler_tick`(센서가 있을 때 `bme280_read` 결과)에서 갱신한다. `app-main.c`는 BME280 초기화 결과를 `sampler_init`의 네 번째 인자로 넘긴다. SPI 초기화가 실패하면 `BME280_ERR_BUS`를 넘긴다(원인 구분은 Task 3의 app-main 몫).

- [ ] **Step 4: 통과 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1`
Expected: `ALL PASSED`

- [ ] **Step 5: 커밋**

```bash
git add firmware/main/sampler.h firmware/main/sampler.c firmware/main/app-main.c firmware/test_host/test-sampler.c
git commit -m "feat: 샘플러가 센서 초기화·측정 결과 코드를 기억"
```

### Task 3: get_status에 sensor_error·sensor_chip_id 추가

**Files:**
- Modify: `firmware/main/protocol.h` (`protocol_ops_t`), `firmware/main/protocol.c` (`cmd_get_status`)
- Modify: `firmware/main/app-main.c` (SPI 성공 여부 기록, ops 두 개)
- Modify: `docs/protocol.md` (`get_status` 예시와 필드 표)
- Test: `firmware/test_host/test-protocol.c`

**Interfaces:**
- Consumes: `sampler_sensor_rc()` (Task 2), `bme280_t.last_chip_id` (Task 1)
- Produces: `protocol_ops_t`에 `sensor_ok` 뒤로 두 멤버를 추가한다.
  - `const char *(*sensor_error)(void *ctx);   // "none" | "spi_init" | "bus" | "chip_id" | "timeout"`
  - `int (*sensor_chip_id)(void *ctx);          // 0..255, or -1 when never read (emitted as null)`

- [ ] **Step 1: 실패하는 테스트 작성.** fake 구조체에 `const char *sensor_error; int sensor_chip_id;`를 추가하고, `setUp`에서 `"none"`과 `0x60`으로 초기화한 뒤 ops에 연결한다. `test_get_status`의 기대 문자열에서 `"store_ok":true,` 뒤에 `"sensor_error":"none","sensor_chip_id":96,`를 넣는다. 아래 테스트를 추가한다.

```c
void test_get_status_reports_sensor_failure(void) {
    fake.sensor_ok = false; fake.sensor_error = "chip_id"; fake.sensor_chip_id = 0xFF;
    TEST_ASSERT_NOT_NULL(strstr(handle("{\"cmd\":\"get_status\"}"),
        "\"sensor_ok\":false,\"store_ok\":true,\"sensor_error\":\"chip_id\",\"sensor_chip_id\":255,"));
}

void test_get_status_chip_id_null_when_never_read(void) {
    fake.sensor_ok = false; fake.sensor_error = "spi_init"; fake.sensor_chip_id = -1;
    TEST_ASSERT_NOT_NULL(strstr(handle("{\"cmd\":\"get_status\"}"),
        "\"sensor_error\":\"spi_init\",\"sensor_chip_id\":null,"));
}
```

- [ ] **Step 2: 실패 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1 -Name test-protocol`
Expected: 컴파일 실패(`protocol_ops_t has no member named 'sensor_error'`)

- [ ] **Step 3: protocol 구현.** `cmd_get_status`가 `store_ok` 뒤에 두 필드를 출력하게 한다. chip ID가 음수면 `null`을 출력한다.

- [ ] **Step 4: app-main 구현.**
  - `static bool g_spi_ok;`를 두고 `spi_bus_bme280_init`이 성공하면 true로 설정한다.
  - `op_sensor_error`: `!g_spi_ok`이면 `"spi_init"`. 아니면 `sampler_sensor_rc()`를 `BME280_OK→"none"`, `BME280_ERR_BUS→"bus"`, `BME280_ERR_CHIP_ID→"chip_id"`, `BME280_ERR_TIMEOUT→"timeout"`으로 매핑한다. 그 밖의 값은 `"bus"`로 매핑한다.
  - `op_sensor_chip_id`: `g_spi_ok ? g_sensor.last_chip_id : -1`
  - 두 op를 `g_ops`에 연결한다.

- [ ] **Step 5: 문서 갱신.** `docs/protocol.md`의 `get_status` 예시 응답에 `"sensor_error":"none","sensor_chip_id":96`을 넣고, 필드 표에 두 행을 추가한다(값 목록, `null` 조건, 초기화 실패 시 리셋 전까지 유지, `read_now`가 chip ID를 갱신할 수 있음 — 설계 문서 "프로토콜" 절 그대로).

- [ ] **Step 6: 호스트 테스트와 펌웨어 빌드 확인**

Run: `powershell -File firmware\test_host\run-tests.ps1`
Expected: `ALL PASSED`

Run (PowerShell): `. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"; cd firmware; idf.py build`
Expected: `Project build complete.`

- [ ] **Step 7: 커밋**

```bash
git add firmware/main/protocol.h firmware/main/protocol.c firmware/main/app-main.c firmware/test_host/test-protocol.c docs/protocol.md
git commit -m "feat: get_status에 센서 실패 원인과 chip ID 추가"
```

### Task 4: bme280-tool status에 원인 표시

**Files:**
- Modify: `pc/internal/cli/cli.go` (`cmdStatus`의 `Sensor:` 줄)
- Test: `pc/internal/cli/cli_test.go`

**Interfaces:**
- Consumes: `get_status`의 `sensor_error`(string), `sensor_chip_id`(number 또는 null)
- Produces: `func sensorLabel(st map[string]any) string` (`cli.go` 내부 함수, `GetStatus` 결과 타입을 그대로 받는다)
  - `sensor_ok`가 true → `"ok"`
  - false이고 `sensor_error`가 없거나 `""`/`"none"` → `"error"`
  - `"chip_id"`이고 `sensor_chip_id`가 숫자 → `fmt.Sprintf("error (chip_id: read 0x%02X, expected 0x60)", id)`
  - 그 밖 → `"error (" + sensor_error + ")"`

- [ ] **Step 1: 실패하는 테스트 작성**

```go
func TestStatusPrintsSensorFailureCause(t *testing.T) {
	cases := []struct{ fields, want string }{
		{`"sensor_ok":false,"sensor_error":"chip_id","sensor_chip_id":255`, "Sensor: error (chip_id: read 0xFF, expected 0x60)\n"},
		{`"sensor_ok":false,"sensor_error":"chip_id","sensor_chip_id":0`, "Sensor: error (chip_id: read 0x00, expected 0x60)\n"},
		{`"sensor_ok":false,"sensor_error":"spi_init","sensor_chip_id":null`, "Sensor: error (spi_init)\n"},
		{`"sensor_ok":false,"sensor_error":"brownout","sensor_chip_id":null`, "Sensor: error (brownout)\n"},
		{`"sensor_ok":false`, "Sensor: error\n"},
		{`"sensor_ok":true,"sensor_error":"none","sensor_chip_id":96`, "Sensor: ok\n"},
	}
	for _, c := range cases {
		factory, _ := factoryWith(t, strings.Replace(status, `"sensor_ok":true`, c.fields, 1))
		_, out, _ := run(t, factory, "", "--port", "COM9", "status")
		if !strings.Contains(out, c.want) {
			t.Fatalf("fields %s: want %q in %q", c.fields, c.want, out)
		}
	}
}
```

- [ ] **Step 2: 실패 확인**

Run: `cd pc; go test ./internal/cli -run TestStatusPrintsSensorFailureCause`
Expected: FAIL (`want "Sensor: error (chip_id: read 0xFF, ...` in 출력)

- [ ] **Step 3: 구현.** `sensorLabel`을 추가하고 `cmdStatus`의 `Sensor:` 줄에서 사용한다. 숫자 판별은 `client.Int64`가 아니라 `st["sensor_chip_id"]`의 타입(`json.Number` 등 client가 쓰는 타입)으로 null과 구분한다.

- [ ] **Step 4: 통과 확인**

Run: `cd pc; go test ./...`
Expected: `ok` (전체)

- [ ] **Step 5: 커밋**

```bash
git add pc/internal/cli/cli.go pc/internal/cli/cli_test.go
git commit -m "feat: bme280-tool status에 센서 실패 원인 표시"
```

### Task 5: 웹 페이지 원인 표시와 README

**Files:**
- Modify: `firmware/main/web/index.html` (상태 문자열의 `'센서 ' + ...` 부분)
- Modify: `README.md` (상태 LED 표의 빨강 행)

**Interfaces:**
- Consumes: `get_status`의 `sensor_error`, `sensor_chip_id`

- [ ] **Step 1: 구현.** 센서 표시를 다음 규칙의 함수(예: `sensorText(s)`)로 만든다.
  - `s.sensor_ok` → `'정상'`
  - `sensor_error`가 `'chip_id'`이고 `s.sensor_chip_id != null` → `'오류 (chip ID 0x' + hex2(id) + ', 기대값 0x60)'` (`hex2`는 대문자 두 자리, 0도 `00`)
  - `bus` → `'오류 (통신 실패)'`, `timeout` → `'오류 (측정 시간 초과)'`, `spi_init` → `'오류 (SPI 초기화 실패)'`
  - 필드가 없거나 `'none'` → `'오류'`
  - 그 밖의 값 → `'오류 (' + 값 + ')'`

  최종 문구는 `센서 오류 (chip ID 0xFF, 기대값 0x60)` 형태가 된다.

- [ ] **Step 2: README 갱신.** 상태 LED 표의 빨강 행 의미 뒤에 "원인은 `bme280-tool status`의 `Sensor:` 줄 또는 웹 상태에서 확인"을 덧붙인다.

- [ ] **Step 3: 펌웨어 빌드 확인** (웹 페이지는 펌웨어에 임베드됨)

Run (PowerShell): `. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"; cd firmware; idf.py build`
Expected: `Project build complete.`

- [ ] **Step 4: 커밋**

```bash
git add firmware/main/web/index.html README.md
git commit -m "feat: 웹 상태에 센서 실패 원인 표시"
```

### Task 6: 실장치 확인 (COM9)

구현 서브에이전트가 아니라 메인 세션에서 직접 한다.

- [ ] **Step 1: 플래시.** `cd firmware; idf.py -p COM9 flash`
- [ ] **Step 2: 확인.** `cd pc; go build -o bme280-tool.exe ./cmd/bme280-tool; .\bme280-tool.exe --port COM9 status`
  Expected: `Sensor:` 줄에 원인이 표시되고, `get_status` 원문에 `sensor_error`·`sensor_chip_id`가 있다. 결과(원인과 chip ID)를 사용자에게 보고한다.
