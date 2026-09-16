package cli

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"bme280-tool/internal/transport"
)

const (
	ping    = `{"ok":true,"firmware":"0.1.0","boot_id":3,"uptime_s":1000,"time_valid":false}`
	setTime = `{"ok":true,"boot_id":3,"uptime_s":1000}`
	status  = `{"ok":true,"count":1234,"capacity":32512,"interval_s":60,"sensor_ok":true,"store_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121}`
)

func factoryWith(t *testing.T, responses ...string) (Factory, *transport.Fake) {
	t.Helper()
	f := transport.NewFake(append([]string{ping, setTime}, responses...)...)
	return func(port string) transport.Transport {
		if port != "COM9" {
			t.Fatalf("port %q", port)
		}
		return f
	}, f
}

func run(t *testing.T, factory Factory, stdin string, args ...string) (code int, out, errOut string) {
	t.Helper()
	var o, e bytes.Buffer
	code = Run(args, &o, &e, strings.NewReader(stdin), factory)
	return code, o.String(), e.String()
}

func sent(t *testing.T, f *transport.Fake, i int) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal([]byte(f.Sent[i]), &m); err != nil {
		t.Fatalf("sent[%d] = %q", i, f.Sent[i])
	}
	return m
}

func nonEmptyLines(s string) []string {
	var out []string
	for _, l := range strings.Split(s, "\n") {
		if strings.TrimSpace(l) != "" {
			out = append(out, l)
		}
	}
	return out
}

func logPage(total, offset int, recs [][]int64) string {
	var parts []string
	for _, r := range recs {
		parts = append(parts, fmt.Sprintf("[%d,%d,%d,%d,%d,%d]", r[0], r[1], r[2], r[3], r[4], r[5]))
	}
	return fmt.Sprintf(`{"ok":true,"total":%d,"offset":%d,"records":[%s]}`, total, offset, strings.Join(parts, ","))
}

func TestConnectSyncsTimeFirst(t *testing.T) {
	factory, f := factoryWith(t, `{"ok":true,"temp_c":23.45,"hum_pct":41.20,"pressure_pa":101325}`)
	if code, _, e := run(t, factory, "", "--port", "COM9", "now"); code != 0 {
		t.Fatalf("code %d stderr %q", code, e)
	}
	if sent(t, f, 0)["cmd"] != "ping" {
		t.Fatal("first request must be ping")
	}
	st := sent(t, f, 1)
	if st["cmd"] != "set_time" || abs(st["epoch"].(float64)-float64(time.Now().Unix())) > 5 {
		t.Fatalf("set_time %v", st)
	}
}

func abs(x float64) float64 {
	if x < 0 {
		return -x
	}
	return x
}

func TestNowPrintsReading(t *testing.T) {
	factory, _ := factoryWith(t, `{"ok":true,"temp_c":23.45,"hum_pct":41.20,"pressure_pa":101325}`)
	_, out, _ := run(t, factory, "", "--port", "COM9", "now")
	for _, want := range []string{"Temperature", "23.45", "Humidity", "41.20", "Pressure", "1013.25"} {
		if !strings.Contains(out, want) {
			t.Fatalf("missing %q in %q", want, out)
		}
	}
}

func TestStatusPrintsFields(t *testing.T) {
	factory, _ := factoryWith(t, status)
	_, out, _ := run(t, factory, "", "--port", "COM9", "status")
	for _, want := range []string{"1234 / 32512", "Interval: 60s", "Sensor: ok", "Store: ok", "Time sync: done", "Boot #3, uptime 00:02:01"} {
		if !strings.Contains(out, want) {
			t.Fatalf("missing %q in %q", want, out)
		}
	}
}

func TestLogTableNewestFirst(t *testing.T) {
	var recs [][]int64
	for i := int64(0); i < 3; i++ {
		recs = append(recs, []int64{1789000000 + i*60, 2000 + i, 5000, 101300, 1, 3})
	}
	factory, _ := factoryWith(t, logPage(3, 0, recs))
	_, out, _ := run(t, factory, "", "--port", "COM9", "log")
	lines := nonEmptyLines(out)
	if len(lines) != 4 || !strings.Contains(lines[0], "Time") || !strings.Contains(lines[1], "20.02") || !strings.Contains(lines[3], "20.00") {
		t.Fatalf("lines %q", lines)
	}
}

func TestLogLastNFetchesTailOnly(t *testing.T) {
	var recs [][]int64
	for i := int64(1195); i < 1200; i++ {
		recs = append(recs, []int64{1789000000 + i, 2000, 5000, 101300, 1, 3})
	}
	factory, f := factoryWith(t, strings.Replace(status, `"count":1234`, `"count":1200`, 1), logPage(1200, 1195, recs))
	_, out, _ := run(t, factory, "", "--port", "COM9", "log", "--last", "5")
	if got := sent(t, f, 3); got["cmd"] != "get_log" || got["offset"] != float64(1195) || got["limit"] != float64(500) {
		t.Fatalf("sent %v", got)
	}
	if n := len(nonEmptyLines(out)); n != 6 {
		t.Fatalf("lines %d", n)
	}
}

func TestLogSinceFilters(t *testing.T) {
	base := time.Date(2026, 9, 16, 12, 0, 0, 0, time.Local).Unix()
	var recs [][]int64
	for i := int64(0); i < 4; i++ {
		recs = append(recs, []int64{base + i*3600, 2000, 5000, 101300, 1, 3})
	}
	factory, _ := factoryWith(t, logPage(4, 0, recs))
	_, out, _ := run(t, factory, "", "--port", "COM9", "log", "--since", "2026-09-16T14:00")
	if n := len(nonEmptyLines(out)); n != 3 {
		t.Fatalf("lines %d: %q", n, out)
	}
}

func TestLogCSVWritesFile(t *testing.T) {
	recs := [][]int64{{1789000000, 2345, 4120, 101325, 1, 3}, {700, 2000, 5000, 101300, 0, 3}}
	factory, _ := factoryWith(t, logPage(2, 0, recs))
	path := filepath.Join(t.TempDir(), "out.csv")
	_, out, _ := run(t, factory, "", "--port", "COM9", "log", "--csv", path)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	lines := nonEmptyLines(strings.ReplaceAll(string(data), "\r\n", "\n"))
	if lines[0] != "timestamp,temp_c,hum_pct,pressure_pa,time_valid,boot_id" || len(lines) != 3 {
		t.Fatalf("csv %q", lines)
	}
	if !strings.Contains(out, "Saved 2 records") {
		t.Fatalf("out %q", out)
	}
}

func TestClearRequiresConfirmation(t *testing.T) {
	factory, f := factoryWith(t, `{"ok":true}`)
	code, out, _ := run(t, factory, "n\n", "--port", "COM9", "clear")
	if code != 0 || !strings.Contains(out, "Cancelled") {
		t.Fatalf("code %d out %q", code, out)
	}
	for i := range f.Sent {
		if sent(t, f, i)["cmd"] == "clear_log" {
			t.Fatal("clear_log must not be sent")
		}
	}
}

func TestClearWithYes(t *testing.T) {
	factory, f := factoryWith(t, `{"ok":true}`)
	code, out, _ := run(t, factory, "", "--port", "COM9", "clear", "--yes")
	if code != 0 || sent(t, f, 2)["cmd"] != "clear_log" || !strings.Contains(out, "Records deleted") {
		t.Fatalf("code %d out %q sent %v", code, out, f.Sent)
	}
}

func TestIntervalGetAndSet(t *testing.T) {
	factory, _ := factoryWith(t, status)
	_, out, _ := run(t, factory, "", "--port", "COM9", "interval")
	if !strings.Contains(out, "Interval: 60s") {
		t.Fatalf("out %q", out)
	}
	factory, f := factoryWith(t, `{"ok":true}`)
	code, out, _ := run(t, factory, "", "--port", "COM9", "interval", "120")
	if code != 0 || sent(t, f, 2)["interval_s"] != float64(120) || !strings.Contains(out, "Interval set to 120s") {
		t.Fatalf("code %d out %q", code, out)
	}
}

func TestDeviceErrorIsReported(t *testing.T) {
	factory, _ := factoryWith(t, `{"ok":false,"error":"sensor_error"}`)
	code, _, e := run(t, factory, "", "--port", "COM9", "now")
	if code != 1 || !strings.Contains(e, "sensor_error") {
		t.Fatalf("code %d stderr %q", code, e)
	}
}

func TestUnknownCommandExits2(t *testing.T) {
	factory, _ := factoryWith(t)
	if code, _, _ := run(t, factory, "", "--port", "COM9", "bogus"); code != 2 {
		t.Fatalf("code %d", code)
	}
}

func TestLogSinceInvalidExits2(t *testing.T) {
	factory, _ := factoryWith(t, logPage(0, 0, nil))
	code, _, e := run(t, factory, "", "--port", "COM9", "log", "--since", "not-a-date")
	if code != 2 || !strings.Contains(e, "Error:") {
		t.Fatalf("code %d stderr %q", code, e)
	}
}

func TestIntervalNonNumericExits2(t *testing.T) {
	factory, _ := factoryWith(t)
	code, _, _ := run(t, factory, "", "--port", "COM9", "interval", "abc")
	if code != 2 {
		t.Fatalf("code %d", code)
	}
}
