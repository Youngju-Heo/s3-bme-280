// Package cli implements the bme280-tool commands with Korean user-facing output.
package cli

import (
	"bufio"
	"encoding/csv"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"time"

	"bme280-tool/internal/client"
	"bme280-tool/internal/records"
	"bme280-tool/internal/transport"
)

const defaultPort = "COM9"

const usage = `사용법: bme280-tool [--port COM9] <command>

  status                             장치 상태
  now                                즉시 측정값
  log [--last N] [--since ISO8601] [--csv PATH]
                                     이력 조회(기본 표 출력, 최신순)
  clear [--yes]                      이력 삭제(확인 프롬프트)
  interval [SECONDS]                 측정 주기 조회/변경
`

type Factory func(port string) transport.Transport

type session struct {
	client *client.DeviceClient
	sync   records.SyncInfo
	stdout io.Writer
	stdin  io.Reader
}

type command func(s *session, args []string) (int, error)

var commands = map[string]command{
	"status":   cmdStatus,
	"now":      cmdNow,
	"log":      cmdLog,
	"clear":    cmdClear,
	"interval": cmdInterval,
}

// Run parses args, connects (ping + time sync) and dispatches one command. Exit codes: 0 ok,
// 1 device/port error, 2 usage error.
func Run(args []string, stdout, stderr io.Writer, stdin io.Reader, factory Factory) int {
	fs := flag.NewFlagSet("bme280-tool", flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.Usage = func() { fmt.Fprint(stderr, usage) }
	port := fs.String("port", defaultPort, "시리얼 포트")
	if err := fs.Parse(args); err != nil {
		return 2
	}
	rest := fs.Args()
	if len(rest) == 0 {
		fs.Usage()
		return 2
	}
	handler, ok := commands[rest[0]]
	if !ok {
		fmt.Fprintf(stderr, "알 수 없는 명령: %s\n", rest[0])
		fs.Usage()
		return 2
	}
	if factory == nil {
		factory = func(p string) transport.Transport { return transport.NewSerial(p, 1*time.Second) }
	}

	c := client.New(factory(*port))
	if err := c.Open(); err != nil {
		fmt.Fprintf(stderr, "오류: 포트를 열 수 없습니다 (%v)\n", err)
		return 1
	}
	defer c.Close()

	sync, err := connect(c)
	if err != nil {
		return reportError(stderr, err)
	}
	code, err := handler(&session{client: c, sync: sync, stdout: stdout, stdin: stdin}, rest[1:])
	if err != nil {
		return reportError(stderr, err)
	}
	return code
}

func connect(c *client.DeviceClient) (records.SyncInfo, error) {
	if _, err := c.Ping(); err != nil {
		return records.SyncInfo{}, err
	}
	reply, err := c.SetTime(time.Now().Unix())
	if err != nil {
		return records.SyncInfo{}, err
	}
	return records.SyncInfo{BootID: int(client.Int64(reply["boot_id"])), UptimeS: client.Int64(reply["uptime_s"]), PCTime: time.Now().Truncate(time.Second)}, nil
}

func reportError(stderr io.Writer, err error) int {
	var de *client.DeviceError
	if errors.As(err, &de) {
		fmt.Fprintf(stderr, "오류: 장치 응답 실패 (%s)\n", de.Code)
	} else {
		fmt.Fprintf(stderr, "오류: %v\n", err)
	}
	return 1
}

func hms(seconds int64) string {
	return fmt.Sprintf("%02d:%02d:%02d", seconds/3600, (seconds%3600)/60, seconds%60)
}

func cmdStatus(s *session, args []string) (int, error) {
	st, err := s.client.GetStatus()
	if err != nil {
		return 1, err
	}
	fmt.Fprintf(s.stdout, "저장 레코드: %d / %d\n", client.Int64(st["count"]), client.Int64(st["capacity"]))
	fmt.Fprintf(s.stdout, "측정 주기: %d초\n", client.Int64(st["interval_s"]))
	fmt.Fprintf(s.stdout, "센서 상태: %s\n", okLabel(client.Bool(st["sensor_ok"], false)))
	fmt.Fprintf(s.stdout, "저장소 상태: %s\n", okLabel(client.Bool(st["store_ok"], true)))
	fmt.Fprintf(s.stdout, "시간 동기화: %s\n", map[bool]string{true: "완료", false: "미완료"}[client.Bool(st["time_valid"], false)])
	fmt.Fprintf(s.stdout, "부팅 세대: %d, 가동 시간: %s\n", client.Int64(st["boot_id"]), hms(client.Int64(st["uptime_s"])))
	return 0, nil
}

func okLabel(ok bool) string {
	if ok {
		return "정상"
	}
	return "오류"
}

func cmdNow(s *session, args []string) (int, error) {
	r, err := s.client.ReadNow()
	if err != nil {
		return 1, err
	}
	fmt.Fprintf(s.stdout, "온도: %.2f °C\n", client.Float(r["temp_c"]))
	fmt.Fprintf(s.stdout, "습도: %.2f %%\n", client.Float(r["hum_pct"]))
	fmt.Fprintf(s.stdout, "기압: %.2f hPa\n", client.Float(r["pressure_pa"])/100)
	return 0, nil
}

func parseSince(s string) (time.Time, error) {
	for _, layout := range []string{"2006-01-02T15:04:05", "2006-01-02T15:04", "2006-01-02"} {
		if t, err := time.ParseInLocation(layout, s, time.Local); err == nil {
			return t, nil
		}
	}
	return time.Time{}, fmt.Errorf("invalid --since value %q (use 2026-09-16T14:00)", s)
}

func cmdLog(s *session, args []string) (int, error) {
	fs := flag.NewFlagSet("log", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	last := fs.Int("last", -1, "최근 N건만")
	since := fs.String("since", "", "이 시각 이후만 (ISO 8601)")
	csvPath := fs.String("csv", "", "표 대신 CSV 파일로 저장")
	if err := fs.Parse(args); err != nil {
		return 2, err
	}

	offset := 0
	if *last >= 0 {
		st, err := s.client.GetStatus()
		if err != nil {
			return 1, err
		}
		if offset = int(client.Int64(st["count"])) - *last; offset < 0 {
			offset = 0
		}
	}
	raw, _, err := s.client.FetchLog(offset)
	if err != nil {
		return 1, err
	}
	var recs []records.LogRecord
	for _, a := range raw {
		r, err := records.FromArray(a)
		if err != nil {
			return 1, err
		}
		recs = append(recs, r)
	}
	if *since != "" {
		sinceTime, err := parseSince(*since)
		if err != nil {
			return 2, err
		}
		var kept []records.LogRecord
		for _, r := range recs {
			if t, ok := records.ResolveTime(r, s.sync); ok && !t.Before(sinceTime) {
				kept = append(kept, r)
			}
		}
		recs = kept
	}
	for i, j := 0, len(recs)-1; i < j; i, j = i+1, j-1 { // newest first
		recs[i], recs[j] = recs[j], recs[i]
	}

	if *csvPath != "" {
		f, err := os.Create(*csvPath)
		if err != nil {
			return 1, err
		}
		defer f.Close()
		w := csv.NewWriter(f)
		w.UseCRLF = true
		_ = w.Write(records.CSVHeader)
		for _, r := range recs {
			_ = w.Write(records.CSVRow(r, s.sync))
		}
		w.Flush()
		if err := w.Error(); err != nil {
			return 1, err
		}
		fmt.Fprintf(s.stdout, "%d건을 %s에 저장했습니다\n", len(recs), *csvPath)
		return 0, nil
	}

	fmt.Fprintf(s.stdout, "%-22s%10s%10s%12s\n", "시각", "온도(°C)", "습도(%)", "기압(hPa)")
	for _, r := range recs {
		fmt.Fprintf(s.stdout, "%-22s%10.2f%10.2f%12.2f\n", records.FormatTime(r, s.sync), r.TempC(), r.HumPct(), r.PressureHPa())
	}
	return 0, nil
}

func cmdClear(s *session, args []string) (int, error) {
	fs := flag.NewFlagSet("clear", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	yes := fs.Bool("yes", false, "확인 없이 삭제")
	if err := fs.Parse(args); err != nil {
		return 2, err
	}
	if !*yes {
		fmt.Fprint(s.stdout, "장치의 이력을 모두 삭제합니다. 계속할까요? (y/N): ")
		answer, _ := bufio.NewReader(s.stdin).ReadString('\n')
		if strings.ToLower(strings.TrimSpace(answer)) != "y" {
			fmt.Fprintln(s.stdout, "취소했습니다")
			return 0, nil
		}
	}
	if _, err := s.client.ClearLog(); err != nil {
		return 1, err
	}
	fmt.Fprintln(s.stdout, "이력을 삭제했습니다")
	return 0, nil
}

func cmdInterval(s *session, args []string) (int, error) {
	if len(args) == 0 {
		st, err := s.client.GetStatus()
		if err != nil {
			return 1, err
		}
		fmt.Fprintf(s.stdout, "측정 주기: %d초\n", client.Int64(st["interval_s"]))
		return 0, nil
	}
	seconds, err := strconv.Atoi(args[0])
	if err != nil {
		return 2, fmt.Errorf("invalid interval %q", args[0])
	}
	if _, err := s.client.SetInterval(seconds); err != nil {
		return 1, err
	}
	fmt.Fprintf(s.stdout, "측정 주기를 %d초로 설정했습니다\n", seconds)
	return 0, nil
}
