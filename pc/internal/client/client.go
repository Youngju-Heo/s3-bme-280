// Package client speaks the device's newline-delimited JSON protocol.
package client

import (
	"bytes"
	"encoding/json"
	"strings"
	"time"

	"bme280-tool/internal/transport"
)

// DeviceError carries the device's error code ("timeout" when no reply arrived).
type DeviceError struct{ Code string }

func (e *DeviceError) Error() string { return e.Code }

const (
	PageLimit      = 500
	defaultTimeout = 3 * time.Second
	logTimeout     = 10 * time.Second
	clearTimeout   = 30 * time.Second
)

type DeviceClient struct {
	t transport.Transport
}

func New(t transport.Transport) *DeviceClient { return &DeviceClient{t: t} }

func (c *DeviceClient) Open() error  { return c.t.Open() }
func (c *DeviceClient) Close() error { return c.t.Close() }

// Request sends one command and waits for the matching reply, ignoring lines that are not
// protocol replies (boot logs, blank lines). A reply with ok:false becomes a DeviceError.
func (c *DeviceClient) Request(cmd string, timeout time.Duration, params map[string]any) (map[string]any, error) {
	req := map[string]any{"cmd": cmd}
	for k, v := range params {
		req[k] = v
	}
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(req); err != nil {
		return nil, err
	}
	if err := c.t.SendLine(strings.TrimRight(buf.String(), "\n")); err != nil {
		return nil, err
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		line, err := c.t.ReadLine()
		if err != nil {
			return nil, err
		}
		if line == "" {
			continue
		}
		msg, ok := parseReply(line)
		if !ok {
			continue
		}
		if !Bool(msg["ok"], false) {
			code, _ := msg["error"].(string)
			if code == "" {
				code = "unknown"
			}
			return nil, &DeviceError{Code: code}
		}
		return msg, nil
	}
	return nil, &DeviceError{Code: "timeout"}
}

// parseReply accepts a JSON object with an "ok" key, retrying from the first '{' so a
// boot-log fragment glued in front of the reply does not hide it.
func parseReply(line string) (map[string]any, bool) {
	msg, ok := decodeObject(line)
	if !ok {
		if i := strings.IndexByte(line, '{'); i > 0 {
			msg, ok = decodeObject(line[i:])
		}
	}
	if !ok {
		return nil, false
	}
	if _, has := msg["ok"]; !has {
		return nil, false
	}
	return msg, true
}

func decodeObject(s string) (map[string]any, bool) {
	dec := json.NewDecoder(strings.NewReader(s))
	dec.UseNumber()
	var m map[string]any
	if err := dec.Decode(&m); err != nil || m == nil {
		return nil, false
	}
	return m, true
}

func (c *DeviceClient) Ping() (map[string]any, error) { return c.Request("ping", defaultTimeout, nil) }
func (c *DeviceClient) SetTime(epoch int64) (map[string]any, error) {
	return c.Request("set_time", defaultTimeout, map[string]any{"epoch": epoch})
}
func (c *DeviceClient) ReadNow() (map[string]any, error)   { return c.Request("read_now", defaultTimeout, nil) }
func (c *DeviceClient) GetStatus() (map[string]any, error) { return c.Request("get_status", defaultTimeout, nil) }
func (c *DeviceClient) GetLog(offset, limit int) (map[string]any, error) {
	return c.Request("get_log", logTimeout, map[string]any{"offset": offset, "limit": limit})
}
func (c *DeviceClient) ClearLog() (map[string]any, error) { return c.Request("clear_log", clearTimeout, nil) }
func (c *DeviceClient) SetInterval(seconds int) (map[string]any, error) {
	return c.Request("set_interval", defaultTimeout, map[string]any{"interval_s": seconds})
}
func (c *DeviceClient) SetWifi(ssid, password string) (map[string]any, error) {
	return c.Request("set_wifi", defaultTimeout, map[string]any{"ssid": ssid, "password": password})
}

// FetchLog reads every record from offset to the end, advancing by PageLimit per request
// (not by the number of records returned, since CRC-skipped slots can shorten a page).
func (c *DeviceClient) FetchLog(offset int) ([][]int64, int, error) {
	pageReply, err := c.GetLog(offset, PageLimit)
	if err != nil {
		return nil, 0, err
	}
	total := int(Int64(pageReply["total"]))
	records := recordsOf(pageReply)
	for offset += PageLimit; offset < total; offset += PageLimit {
		pageReply, err = c.GetLog(offset, PageLimit)
		if err != nil {
			return nil, 0, err
		}
		records = append(records, recordsOf(pageReply)...)
	}
	return records, total, nil
}

func recordsOf(reply map[string]any) [][]int64 {
	raw, _ := reply["records"].([]any)
	out := make([][]int64, 0, len(raw))
	for _, r := range raw {
		fields, _ := r.([]any)
		rec := make([]int64, len(fields))
		for i, f := range fields {
			rec[i] = Int64(f)
		}
		out = append(out, rec)
	}
	return out
}

func Int64(v any) int64 {
	switch n := v.(type) {
	case json.Number:
		i, err := n.Int64()
		if err != nil {
			f, _ := n.Float64()
			return int64(f)
		}
		return i
	case float64:
		return int64(n)
	}
	return 0
}

func Float(v any) float64 {
	switch n := v.(type) {
	case json.Number:
		f, _ := n.Float64()
		return f
	case float64:
		return n
	}
	return 0
}

func Bool(v any, def bool) bool {
	if b, ok := v.(bool); ok {
		return b
	}
	return def
}
