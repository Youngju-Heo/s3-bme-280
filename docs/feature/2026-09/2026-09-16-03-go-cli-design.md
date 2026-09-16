# bme280-tool Go 재작성 설계

## 원 요청

> 한가지더 요청이 있는데, bmd280-cli 를 go 로 재작업

## 확정된 요구사항

| 항목 | 결정 | 근거 |
|---|---|---|
| 범위 | Python `bme280-tool`과 **동일한 명령·출력·종료 코드**의 Go CLI. 기능 추가 없음 | 재작성이므로 동작 동등성이 성공 기준. 단, 출력 텍스트는 콘솔 도구 규칙에 따라 영어로 작성한다(Python의 한국어 출력은 규칙 오독이었음) |
| Python 처리 | 완료 후 `pc/`의 Python 파일 삭제, 같은 경로에 Go 프로젝트 | 유지보수 대상 하나 |
| 의존성 | `go.bug.st/serial` 하나. CLI는 표준 `flag`, JSON은 `encoding/json` | 바이너리 작고 빌드 단순 |
| 진행 순서 | 이 작업 → 이후 WiFi 웹(설계 `2026-09-16-02`)의 CLI 부분을 Go 기준으로 수정 | 이중 작업 방지 |

## 구조

```
pc/
  go.mod                        module bme280-tool, go 1.25
  cmd/bme280-tool/main.go       os.Args → cli.Run(...) → os.Exit
  internal/transport/           Transport 인터페이스, SerialTransport(go.bug.st/serial)
  internal/client/              DeviceClient, DeviceError
  internal/records/             LogRecord, SyncInfo, 시각 해석, CSV
  internal/cli/                 Run(args, stdout, stderr, stdin, factory) int, 명령 5개
```

### transport

```go
type Transport interface {
    Open() error
    Close() error
    SendLine(line string) error
    ReadLine() (string, error)   // '\n'까지. 타임아웃이면 ("", nil)
}
func NewSerial(port string, timeout time.Duration) *SerialTransport
```

- 열 때 `serial.Mode{BaudRate: 115200, InitialStatusBits: &serial.ModemOutputBits{RTS: false, DTR: false}}` — USB Serial/JTAG의 DTR/RTS 리셋 방지(Python `dtr=False, rts=False`에 대응). 열린 뒤 `ResetInputBuffer()`.
- `ReadLine`은 `bufio.Reader`로 `\n`까지 읽고 `\r\n`·공백을 제거. 읽기 타임아웃은 `SetReadTimeout(timeout)`; 타임아웃 시 빈 문자열·nil 오류.

### client

```go
type DeviceError struct{ Code string }   // Error() = Code ("timeout", "unknown_cmd", ...)
type DeviceClient struct{ t Transport }
const PageLimit = 500
func (c *DeviceClient) Request(cmd string, timeout time.Duration, params map[string]any) (map[string]any, error)
func (c *DeviceClient) Ping/SetTime(epoch int64)/ReadNow/GetStatus/GetLog(offset, limit int)/ClearLog/SetInterval(sec int)
func (c *DeviceClient) FetchLog(offset int) (records [][]int64, total int, err error)
```

- 요청 JSON: `{"cmd":...}` + params (snake_case 키). `encoding/json`은 키를 정렬하므로 순서는 `cmd`가 앞에 오지 않을 수 있음 — 장치 파서는 키 순서에 무관.
- 응답: `json.Decoder.UseNumber()`로 정수 보존. 줄이 JSON 객체가 아니거나 `ok` 키가 없으면 무시. 파싱 실패 시 첫 `{`부터 재시도(부트 로그 파편). `ok:false` → `DeviceError{error}`. 마감까지 응답 없으면 `DeviceError{"timeout"}`.
- 타임아웃: 기본 3초, `get_log` 10초, `clear_log` 30초. `FetchLog`는 offset을 `PageLimit`씩 올려 `total`까지.

### records

```go
type LogRecord struct{ Timestamp int64; TempCenti, HumCenti int; PressurePa int64; Flags, BootID int }
func FromArray(a []int64) (LogRecord, error)      // 길이 6 검사
func (r LogRecord) TimeValid() bool; TempC() float64; HumPct() float64; PressureHPa() float64
type SyncInfo struct{ BootID int; UptimeS int64; PCTime time.Time }
func ResolveTime(r LogRecord, s SyncInfo) (time.Time, bool)
func FormatTime(r LogRecord, s SyncInfo) string   // "2006-01-02 15:04:05" / "~..." / "boot#N +HH:MM:SS"
var CSVHeader = []string{"timestamp","temp_c","hum_pct","pressure_pa","time_valid","boot_id"}
func CSVRow(r LogRecord, s SyncInfo) []string       // 시각은 RFC3339 대신 Python isoformat과 같은 "2006-01-02T15:04:05"
```

### cli

- `Run(args []string, stdout, stderr io.Writer, stdin io.Reader, factory func(port string) client.Transport) int`
- 전역 플래그 `--port`(기본 `COM9`), 서브커맨드 `status | now | log [--last N] [--since ISO8601] [--csv PATH] | clear [--yes] | interval [SECONDS]`. 각 서브커맨드는 `flag.NewFlagSet`.
- 접속: `Ping` → `SetTime(now)` → `SyncInfo{boot_id, uptime_s, time.Now()}`.
- 출력 문자열은 영어다(콘솔 도구 규칙; UI가 아니므로 한국어 대상이 아님). 표 헤더/폭: `Time`(22칸 좌) `Temp(°C)`(10 우) `Hum(%)`(10 우) `Press(hPa)`(12 우). 최신순.
- 오류: `DeviceError` → stderr `오류: 장치 응답 실패 (<code>)` 종료 1; 포트 열기 실패 → `오류: 포트를 열 수 없습니다 (<err>)` 종료 1.
- Windows 콘솔 한글: `main.go`에서 `SetConsoleOutputCP(65001)` 호출(`golang.org/x/sys` 없이 `syscall.NewLazyDLL("kernel32")`).

## 테스트

- 표준 `testing`만. `internal/client/client_test.go`, `internal/records/records_test.go`, `internal/cli/cli_test.go`.
- 가짜 트랜스포트(`fakeTransport{responses []string; sent []string}`)를 `client`와 `cli` 테스트가 공유 — `internal/transport/fake.go`로 두어 두 패키지에서 import.
- Python 테스트 24건을 동일 시나리오로 이식: 요청 형식·노이즈 무시·파편 복구·오류·타임아웃·페이지네이션 / 단위·시각 3분기·CSV / 각 명령 출력·전송 순서·`--last` 오프셋·`--since` 필터·CSV 파일·`clear` 확인 프롬프트·오류 종료 코드.
- 실기: `go build` 산출물로 `status → now → interval 10 → (35초) → log --last 5 → log --csv → interval 60 → clear --yes → status`, 그리고 리셋 직후 첫 명령.

## 빌드·정리

- `cd pc; go build -o bme280-tool.exe ./cmd/bme280-tool`. `.gitignore`에 `pc/bme280-tool.exe`.
- 삭제: `pc/pyproject.toml`, `pc/src/`, `pc/tests/`. 갱신: `README.md`(PC 도구 절), `docs/protocol.md`(참조 구현 경로), `.gitignore`(Python 항목 정리).

## 네이밍

| 대상 | 이름 |
|---|---|
| 모듈 | `bme280-tool` |
| 패키지 | `transport`, `client`, `records`, `cli` (`internal/` 아래) |
| 타입 | `Transport`, `SerialTransport`, `DeviceClient`, `DeviceError`, `LogRecord`, `SyncInfo` |
| 파일 | 패키지당 `transport.go`/`fake.go`, `client.go`, `records.go`, `cli.go`, `*_test.go` (Go 관례: 소문자 단일 단어 + `_test` 접미사) |
| 실행 파일 | `bme280-tool.exe` |
