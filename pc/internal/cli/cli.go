// Package cli implements the bme280-tool commands with English console output.
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

const usage = `Usage: bme280-tool [--port COM9] <command>

  status                             device status
  now                                immediate reading
  log [--last N] [--since ISO8601] [--csv PATH]
                                     history (table, newest first)
  clear [--yes]                      delete all records (asks for confirmation)
  interval [SECONDS]                 show or set the sampling interval
`

type Factory func(port string) transport.Transport

type session struct {
	client *client.DeviceClient
	sync   records.SyncInfo
	stdout io.Writer
	stdin  io.Reader
}

// runner executes an already-parsed command against an open session.
type runner func(s *session) (int, error)

var errUnknownCommand = errors.New("unknown command")

// prepare parses a sub-command's arguments and returns a closure that runs it. All argument
// validation happens here, before the port is opened, so a usage error never touches the port.
func prepare(name string, args []string) (runner, error) {
	switch name {
	case "status":
		if err := parseNoArgs("status", args); err != nil {
			return nil, err
		}
		return func(s *session) (int, error) { return cmdStatus(s) }, nil
	case "now":
		if err := parseNoArgs("now", args); err != nil {
			return nil, err
		}
		return func(s *session) (int, error) { return cmdNow(s) }, nil
	case "log":
		p, err := parseLogArgs(args)
		if err != nil {
			return nil, err
		}
		return func(s *session) (int, error) { return cmdLog(s, p) }, nil
	case "clear":
		p, err := parseClearArgs(args)
		if err != nil {
			return nil, err
		}
		return func(s *session) (int, error) { return cmdClear(s, p) }, nil
	case "interval":
		p, err := parseIntervalArgs(args)
		if err != nil {
			return nil, err
		}
		return func(s *session) (int, error) { return cmdInterval(s, p) }, nil
	}
	return nil, errUnknownCommand
}

func parseNoArgs(name string, args []string) error {
	fs := flag.NewFlagSet(name, flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 0 {
		return fmt.Errorf("unexpected argument %q", fs.Arg(0))
	}
	return nil
}

// Run parses args, connects (ping + time sync) and dispatches one command. Exit codes: 0 ok,
// 1 device/port error, 2 usage error.
func Run(args []string, stdout, stderr io.Writer, stdin io.Reader, factory Factory) int {
	fs := flag.NewFlagSet("bme280-tool", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	port := fs.String("port", defaultPort, "serial port")
	if err := fs.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			fmt.Fprint(stdout, usage)
			return 0
		}
		fmt.Fprintf(stderr, "Error: %v\n", err)
		return 2
	}
	rest := fs.Args()
	if len(rest) == 0 {
		fmt.Fprint(stderr, usage)
		return 2
	}

	handler, err := prepare(rest[0], rest[1:])
	if err != nil {
		if errors.Is(err, flag.ErrHelp) {
			fmt.Fprint(stdout, usage)
			return 0
		}
		if errors.Is(err, errUnknownCommand) {
			fmt.Fprintf(stderr, "Unknown command: %s\n", rest[0])
			fmt.Fprint(stderr, usage)
			return 2
		}
		fmt.Fprintf(stderr, "Error: %v\n", err)
		return 2
	}

	if factory == nil {
		factory = func(p string) transport.Transport { return transport.NewSerial(p, 1*time.Second) }
	}

	c := client.New(factory(*port))
	if err := c.Open(); err != nil {
		fmt.Fprintf(stderr, "Error: cannot open port (%v)\n", err)
		return 1
	}
	defer c.Close()

	sync, err := connect(c)
	if err != nil {
		return reportError(stderr, err)
	}
	code, err := handler(&session{client: c, sync: sync, stdout: stdout, stdin: stdin})
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
		fmt.Fprintf(stderr, "Error: device request failed (%s)\n", de.Code)
	} else {
		fmt.Fprintf(stderr, "Error: %v\n", err)
	}
	return 1
}

func hms(seconds int64) string {
	return fmt.Sprintf("%02d:%02d:%02d", seconds/3600, (seconds%3600)/60, seconds%60)
}

func cmdStatus(s *session) (int, error) {
	st, err := s.client.GetStatus()
	if err != nil {
		return 1, err
	}
	fmt.Fprintf(s.stdout, "Records: %d / %d\n", client.Int64(st["count"]), client.Int64(st["capacity"]))
	fmt.Fprintf(s.stdout, "Interval: %ds\n", client.Int64(st["interval_s"]))
	fmt.Fprintf(s.stdout, "Sensor: %s\n", okLabel(client.Bool(st["sensor_ok"], false)))
	fmt.Fprintf(s.stdout, "Store: %s\n", okLabel(client.Bool(st["store_ok"], true)))
	fmt.Fprintf(s.stdout, "Time sync: %s\n", map[bool]string{true: "done", false: "pending"}[client.Bool(st["time_valid"], false)])
	fmt.Fprintf(s.stdout, "Boot #%d, uptime %s\n", client.Int64(st["boot_id"]), hms(client.Int64(st["uptime_s"])))
	return 0, nil
}

func okLabel(ok bool) string {
	if ok {
		return "ok"
	}
	return "error"
}

func cmdNow(s *session) (int, error) {
	r, err := s.client.ReadNow()
	if err != nil {
		return 1, err
	}
	fmt.Fprintf(s.stdout, "Temperature: %.2f °C\n", client.Float(r["temp_c"]))
	fmt.Fprintf(s.stdout, "Humidity: %.2f %%\n", client.Float(r["hum_pct"]))
	fmt.Fprintf(s.stdout, "Pressure: %.2f hPa\n", client.Float(r["pressure_pa"])/100)
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

type logParams struct {
	last     int
	since    time.Time
	hasSince bool
	csv      string
}

func parseLogArgs(args []string) (logParams, error) {
	fs := flag.NewFlagSet("log", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	last := fs.Int("last", -1, "only the last N records")
	since := fs.String("since", "", "records at or after this time (ISO 8601, e.g. 2026-09-16T14:00)")
	csvPath := fs.String("csv", "", "write CSV to this file instead of printing a table")
	if err := fs.Parse(args); err != nil {
		return logParams{}, err
	}
	if fs.NArg() != 0 {
		return logParams{}, fmt.Errorf("unexpected argument %q", fs.Arg(0))
	}
	p := logParams{last: *last, csv: *csvPath}
	if *since != "" {
		t, err := parseSince(*since)
		if err != nil {
			return logParams{}, err
		}
		p.since, p.hasSince = t, true
	}
	return p, nil
}

func cmdLog(s *session, p logParams) (int, error) {
	offset := 0
	if p.last >= 0 {
		st, err := s.client.GetStatus()
		if err != nil {
			return 1, err
		}
		if offset = int(client.Int64(st["count"])) - p.last; offset < 0 {
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
	if p.hasSince {
		var kept []records.LogRecord
		for _, r := range recs {
			if t, ok := records.ResolveTime(r, s.sync); ok && !t.Before(p.since) {
				kept = append(kept, r)
			}
		}
		recs = kept
	}
	for i, j := 0, len(recs)-1; i < j; i, j = i+1, j-1 { // newest first
		recs[i], recs[j] = recs[j], recs[i]
	}

	if p.csv != "" {
		f, err := os.Create(p.csv)
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
		fmt.Fprintf(s.stdout, "Saved %d records to %s\n", len(recs), p.csv)
		return 0, nil
	}

	fmt.Fprintf(s.stdout, "%-22s%10s%10s%12s\n", "Time", "Temp(°C)", "Hum(%)", "Press(hPa)")
	for _, r := range recs {
		fmt.Fprintf(s.stdout, "%-22s%10.2f%10.2f%12.2f\n", records.FormatTime(r, s.sync), r.TempC(), r.HumPct(), r.PressureHPa())
	}
	return 0, nil
}

type clearParams struct {
	yes bool
}

func parseClearArgs(args []string) (clearParams, error) {
	fs := flag.NewFlagSet("clear", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	yes := fs.Bool("yes", false, "delete without confirmation")
	if err := fs.Parse(args); err != nil {
		return clearParams{}, err
	}
	if fs.NArg() != 0 {
		return clearParams{}, fmt.Errorf("unexpected argument %q", fs.Arg(0))
	}
	return clearParams{yes: *yes}, nil
}

func cmdClear(s *session, p clearParams) (int, error) {
	if !p.yes {
		fmt.Fprint(s.stdout, "Delete all records on the device. Continue? (y/N): ")
		answer, _ := bufio.NewReader(s.stdin).ReadString('\n')
		if strings.ToLower(strings.TrimSpace(answer)) != "y" {
			fmt.Fprintln(s.stdout, "Cancelled")
			return 0, nil
		}
	}
	if _, err := s.client.ClearLog(); err != nil {
		return 1, err
	}
	fmt.Fprintln(s.stdout, "Records deleted")
	return 0, nil
}

type intervalParams struct {
	set     bool
	seconds int
}

func parseIntervalArgs(args []string) (intervalParams, error) {
	if len(args) == 0 {
		return intervalParams{}, nil
	}
	if len(args) > 1 {
		return intervalParams{}, fmt.Errorf("unexpected argument %q", args[1])
	}
	seconds, err := strconv.Atoi(args[0])
	if err != nil {
		return intervalParams{}, fmt.Errorf("invalid interval %q", args[0])
	}
	return intervalParams{set: true, seconds: seconds}, nil
}

func cmdInterval(s *session, p intervalParams) (int, error) {
	if !p.set {
		st, err := s.client.GetStatus()
		if err != nil {
			return 1, err
		}
		fmt.Fprintf(s.stdout, "Interval: %ds\n", client.Int64(st["interval_s"]))
		return 0, nil
	}
	if _, err := s.client.SetInterval(p.seconds); err != nil {
		return 1, err
	}
	fmt.Fprintf(s.stdout, "Interval set to %ds\n", p.seconds)
	return 0, nil
}
