# 센서 안정화(재시도·SPI 클럭·LED) 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## 원 요청

> SPI 통신이 되었다가 불량이었다를 가끔 반복 하는걸로 예상 됩니다.

(SPI 클럭 하향과 초기화 자동 재시도 제안에 대해:)

> 센서 안정화 2, 3 진행하고 i2c-bus 브랜치는 삭제

> 만약 통신 링크 결함 또는 배선 결함으로 임계여유가 없어 실패 할 확률은 없을까요?

> 둘다 진행 하는게 어떨까요? 빨강인경우를 재시도 인경우는 깜빡임으로 구분 할 수 있나요?

(설계 요약에 대해:)

> 승인, 진행

**Goal:** 센서 통신이 실패하면 다음 측정 주기에 재초기화 후 측정해 자동으로 회복하고, SPI를 1 MHz로 낮추며, LED로 "재시도 중"(빨강 깜빡임)과 "자동 회복 불가"(빨강 고정)를 구분한다.

**Architecture:** 샘플러가 직전 결과 코드를 보고 다음 주기에 `bme280_init`을 먼저 수행한다. app-main은 SPI 버스가 정상이면 초기화 결과와 상관없이 센서 포인터를 샘플러에 넘긴다. LED 상태는 샘플러의 `sampler_sensor_retrying()`으로 고른다.

**Tech Stack:** ESP-IDF v6.0.1 C, 호스트 테스트 ucrt64 gcc + Unity

**Spec:** `docs/feature/2026-11/2026-11-16-02-sensor-retry-design.md`

## Global Constraints

- 재시도 규칙: 측정 주기가 돌아왔을 때 직전 rc가 `BME280_OK`가 아니면 `bme280_init`을 먼저 하고, 성공하면 측정한다. `spi_init` 실패(센서 포인터 NULL)는 재시도하지 않는다.
- `sensor_error`·`sensor_chip_id`는 재시도 결과로 갱신된다. 항상 같은 시도(초기화 또는 측정)의 rc와 chip ID다.
- `read_now`는 `sampler_sensor_ready()`일 때만 센서를 읽는다.
- LED: 센서 실패이면서 재시도 중이면 빨강 깜빡임(`STATUS_LED_ERROR_RETRYING`, 0.5초 주기 `STATUS_LED_BLINK_HALF_MS`, 밝기 `STATUS_LED_BRIGHTNESS`). `spi_init` 실패나 저장 실패면 빨강 고정(`STATUS_LED_ERROR`). 둘이 겹치면 빨강 고정. 빨강은 WiFi 파랑보다 우선한다.
- SPI 클럭 `SPI_CLOCK_HZ` = 1000000
- 정상 동작 시 측정·기록·LED는 바뀌지 않는다.
- 코드 주석은 영어, 문서는 한국어. 커밋 메시지는 `<type>: <한국어 제목>`, `Co-Authored-By` 줄 금지.

## Review Focus

1. **센서 전원 순간 끊김:** 측정이 실패한 다음 주기에는 읽기 전에 반드시 재초기화(리셋 레지스터 쓰기 포함)해야 한다. 그래야 습도 오버샘플링 설정이 복원된다(Task 1 테스트 `test_reinit_after_read_failure`).
2. **재초기화 실패가 연속될 때:** 매 주기마다 init만 시도하고, 기록은 남기지 않으며, `sensor_ok`는 false로 유지되어야 한다. 무한 루프나 같은 주기 안의 반복 시도가 없어야 한다(Task 1 테스트).
3. **부팅 시 초기화 실패 후 첫 주기:** 첫 tick에서 바로 재시도해 성공하면 그 주기의 기록이 남아야 한다(Task 1 테스트 `test_retries_init_failed_at_boot`).
4. **`sampler_init`의 `sensor_ok` 초기값:** 이제 init이 실패해도 sensor가 non-NULL이므로, 초기값은 `sensor != NULL && sensor_init_rc == BME280_OK`여야 한다. 그렇지 않으면 부팅 직후 첫 주기 전까지 녹색이 잘못 켜진다(Task 1).
5. **LED 우선순위:** 저장 실패와 센서 재시도가 겹치면 빨강 고정이어야 한다(Task 2, app-main → 리뷰에서 확인).

---

### Task 1: 샘플러 재시도

**Files:**
- Modify: `firmware/main/sampler.h`, `firmware/main/sampler.c`
- Modify: `docs/protocol.md` (`sensor_error` 행: "부팅 시 초기화 실패 원인은 리셋 전까지 유지되고, 초기화 성공 후에는 주기 측정마다 갱신된다" → "실패하면 다음 측정 주기에 재초기화 후 측정하며, 그 결과로 갱신된다(`spi_init`은 재시도하지 않음)")
- Test: `firmware/test_host/test-sampler.c`

**Interfaces:**
- Produces:
  - `sampler_init`의 시그니처는 그대로다. `g_sensor_ok`의 초기값은 `sensor != NULL && sensor_init_rc == BME280_OK`이다.
  - `bool sampler_sensor_ready(void);` `g_sensor != NULL && g_sensor_rc == BME280_OK`
  - `bool sampler_sensor_retrying(void);` `g_sensor != NULL && g_sensor_rc != BME280_OK`
  - `sampler_tick`: 주기가 돌아왔을 때 `g_sensor == NULL`이면 지금처럼 ok=false로 끝난다. `g_sensor_rc != BME280_OK`이면 `bme280_init(g_sensor, &bus_copy)`를 수행하고(`bus_copy`는 `g_sensor->bus`의 지역 사본), 그 rc와 chip ID를 기록한다. 실패하면 거기서 끝난다. 그다음은 기존 측정 경로다.

- [ ] **Step 1: 실패하는 테스트 작성** (`RUN_TEST` 등록)

```c
void test_retries_init_failed_at_boot(void) {
    fake_bus_regs[BME280_REG_CHIP_ID] = 0x00;
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, bme280_init(&sensor, &fake_bus));
    sampler_init(&sensor, &store, 60, BME280_ERR_CHIP_ID, sensor.last_chip_id);
    TEST_ASSERT_FALSE(sampler_sensor_ok());
    TEST_ASSERT_TRUE(sampler_sensor_retrying());
    TEST_ASSERT_FALSE(sampler_sensor_ready());
    fake_bus_regs[BME280_REG_CHIP_ID] = BME280_CHIP_ID;   // contact restored
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_TRUE(sampler_sensor_ok());
    TEST_ASSERT_TRUE(sampler_sensor_ready());
    TEST_ASSERT_FALSE(sampler_sensor_retrying());
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
    TEST_ASSERT_EQUAL(0x60, sampler_sensor_chip_id());
}

void test_reinit_after_read_failure(void) {
    fake_bus_regs[BME280_REG_CHIP_ID] = 0xFF;
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, sampler_sensor_rc());
    fake_bus_regs[BME280_REG_CHIP_ID] = BME280_CHIP_ID;
    fake_bus_regs[BME280_REG_CTRL_HUM] = 0x00;   // sensor power blip reset its config
    fake_bus_write_count = 0;
    sampler_tick(60, 60, false, 1);
    TEST_ASSERT_EQUAL_HEX8(0xB6, fake_bus_last_write(BME280_REG_RESET));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_bus_regs[BME280_REG_CTRL_HUM]);
    TEST_ASSERT_TRUE(sampler_sensor_ok());
    TEST_ASSERT_EQUAL_UINT32(1, log_store_count(&store));
}

void test_repeated_init_failure_tries_once_per_interval(void) {
    fake_bus_regs[BME280_REG_CHIP_ID] = 0x00;
    sampler_tick(0, 0, false, 1);              // read fails
    fake_bus_write_count = 0;
    sampler_tick(30, 30, false, 1);            // not due: no bus traffic
    TEST_ASSERT_EQUAL(0, fake_bus_write_count);
    sampler_tick(60, 60, false, 1);            // due: re-init attempted, fails on chip id
    TEST_ASSERT_EQUAL(1, fake_bus_write_count);   // only the reset write
    TEST_ASSERT_EQUAL(BME280_ERR_CHIP_ID, sampler_sensor_rc());
    TEST_ASSERT_EQUAL(0x00, sampler_sensor_chip_id());
    TEST_ASSERT_FALSE(sampler_sensor_ok());
    TEST_ASSERT_TRUE(sampler_sensor_retrying());
    TEST_ASSERT_EQUAL_UINT32(0, log_store_count(&store));
}

void test_no_sensor_is_not_retrying(void) {
    sampler_init(NULL, &store, 60, BME280_ERR_BUS, -1);
    sampler_tick(0, 0, false, 1);
    TEST_ASSERT_FALSE(sampler_sensor_retrying());
    TEST_ASSERT_FALSE(sampler_sensor_ready());
}
```

  기존 `test_sensor_chip_id_snapshots_periodic_read`는 "read_now 스타일의 직접 `bme280_read`가 스냅샷을 바꾸지 않는다"를 확인한다. 이 테스트는 그대로 통과해야 한다. 다만 그 테스트의 +60 tick은 이제 재초기화를 거친다. 기대값(`0x60`)은 같다.

- [ ] **Step 2: 실패 확인.** `powershell -ExecutionPolicy Bypass -File firmware\test_host\run-tests.ps1 -Name test-sampler` → 컴파일 실패(`sampler_sensor_ready` 미정의)
- [ ] **Step 3: 구현** (Interfaces 그대로). `docs/protocol.md`의 `sensor_error` 행 문구도 바꾼다.
- [ ] **Step 4: 통과 확인.** `powershell -ExecutionPolicy Bypass -File firmware\test_host\run-tests.ps1` → `ALL PASSED`
- [ ] **Step 5: 커밋**

```bash
git add firmware/main/sampler.h firmware/main/sampler.c firmware/test_host/test-sampler.c docs/protocol.md
git commit -m "feat: 센서 실패 시 다음 측정 주기에 재초기화 후 측정"
```

### Task 2: LED 재시도 표시, app-main 연결, SPI 1 MHz

**Files:**
- Modify: `firmware/main/status-led-color.h`, `firmware/main/status-led-color.c`
- Test: `firmware/test_host/test-status-led.c`
- Modify: `firmware/main/app-main.c` (`g_sensor_ptr` 제거, `sampler_init` 인자, `op_read_now`, LED 선택)
- Modify: `firmware/main/spi-bus.c` (`SPI_CLOCK_HZ`와 주석)
- Modify: `README.md` (LED 표), `PROJECT_OVERVIEW.md` (sampler 행, 데이터 흐름 1번, `spi-bus.c` 설명에 클럭이 있으면 갱신)

**Interfaces:**
- Consumes: `sampler_sensor_ready()`, `sampler_sensor_retrying()` (Task 1)
- Produces: `STATUS_LED_ERROR_RETRYING`(enum의 `STATUS_LED_ERROR` 다음에 추가, 주석 `// sensor failing, retried every interval: blinking red`). `STATUS_LED_ERROR`의 주석은 `// SPI bus init or store failure: steady red`로 바꾼다.

- [ ] **Step 1: 실패하는 테스트 작성**

```c
void test_error_retrying_blinks_red(void) {
    assert_rgb(status_led_color(STATUS_LED_ERROR_RETRYING, 0), STATUS_LED_BRIGHTNESS, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_ERROR_RETRYING, 499), STATUS_LED_BRIGHTNESS, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_ERROR_RETRYING, 500), 0, 0, 0);
    assert_rgb(status_led_color(STATUS_LED_ERROR_RETRYING, 1000), STATUS_LED_BRIGHTNESS, 0, 0);
}
```

- [ ] **Step 2: 실패 확인.** `... run-tests.ps1 -Name test-status-led` → 컴파일 실패
- [ ] **Step 3: `status-led-color` 구현.** 기존 깜빡임 분기와 같은 방식을 쓴다.
- [ ] **Step 4: app-main 수정.**
  - `sampler_init(g_spi_ok ? &g_sensor : NULL, &g_store, settings_interval_s(), sensor_rc, g_spi_ok ? g_sensor.last_chip_id : -1)`로 바꾸고 `g_sensor_ptr`를 제거한다.
  - `op_read_now`: `if (!sampler_sensor_ready()) return -1;` 다음 `bme280_read(&g_sensor, out)`
  - LED 선택:

```c
status_led_state_t led;
if (!sampler_store_ok()) led = STATUS_LED_ERROR;
else if (!sampler_sensor_ok()) led = sampler_sensor_retrying() ? STATUS_LED_ERROR_RETRYING : STATUS_LED_ERROR;
else { /* existing wifi / time selection unchanged */ }
```

- [ ] **Step 5: SPI 클럭.** `#define SPI_CLOCK_HZ 1000000   // 1 MHz: margin for jumper wiring / degraded contacts (BME280 max 10 MHz)`
- [ ] **Step 6: 문서.** README LED 표의 빨강 행을 두 행으로 나눈다.

| 표시 | 의미 |
|---|---|
| 빨강 깜빡임 (0.5초) | 센서 통신 실패, 다음 측정 주기에 자동 재시도 중. 접촉이 돌아오면 저절로 녹색. 원인은 `bme280-tool status`의 `Sensor:` 줄 또는 웹 상태에서 확인 |
| 빨강 고정 | 자동 회복되지 않는 오류: SPI 버스 초기화 실패 또는 플래시 저장 실패. 리셋·점검 필요 |

  PROJECT_OVERVIEW의 샘플러 설명과 데이터 흐름에 "실패하면 다음 주기에 재초기화 후 측정"을 반영한다.
- [ ] **Step 7: 검증.** `run-tests.ps1` 전체 → `ALL PASSED`. PowerShell 도구로 `powershell -ExecutionPolicy Bypass -Command ". 'C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1' *> `$null; cd C:\work\s3-bme-280\firmware; idf.py build"` → `Project build complete.`
- [ ] **Step 8: 커밋**

```bash
git add firmware/main/status-led-color.h firmware/main/status-led-color.c firmware/test_host/test-status-led.c firmware/main/app-main.c firmware/main/spi-bus.c README.md PROJECT_OVERVIEW.md
git commit -m "feat: 센서 재시도 중 LED 빨강 깜빡임 표시와 SPI 1 MHz 하향"
```

### Task 3: 실장치 확인 (COM9, 메인 세션)

- [ ] **Step 1:** `idf.py -p COM9 flash` 후 `bme280-tool.exe --port COM9 status`에서 `Sensor: ok`가 나오는지 확인한다. 1 MHz에서도 `read_now`를 60번 연속 보내 모두 성공하는지 확인한다.
- [ ] **Step 2:** 사용자에게 재시도 동작 확인 방법을 안내한다. SDO 선을 잠깐 빼면 다음 주기에 빨강 깜빡임이 되고, 다시 꽂으면 그다음 주기에 녹색으로 돌아온다.
