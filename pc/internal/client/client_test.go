package client

import (
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"testing"
	"time"

	"bme280-tool/internal/transport"
)

func sentJSON(t *testing.T, line string) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal([]byte(line), &m); err != nil {
		t.Fatalf("sent line is not JSON: %q", line)
	}
	return m
}

func TestRequestSendsJSONLineAndParsesReply(t *testing.T) {
	f := transport.NewFake(`{"ok":true,"firmware":"0.1.0"}`)
	reply, err := New(f).Request("ping", time.Second, nil)
	if err != nil {
		t.Fatal(err)
	}
	if got := sentJSON(t, f.Sent[0]); got["cmd"] != "ping" || len(got) != 1 {
		t.Fatalf("sent %v", got)
	}
	if reply["firmware"] != "0.1.0" {
		t.Fatalf("reply %v", reply)
	}
}

func TestRequestIncludesParamsWithSnakeCaseKeys(t *testing.T) {
	f := transport.NewFake(`{"ok":true}`)
	_, _ = New(f).GetLog(10, 500)
	got := sentJSON(t, f.Sent[0])
	if got["cmd"] != "get_log" || got["offset"] != float64(10) || got["limit"] != float64(500) {
		t.Fatalf("sent %v", got)
	}
}

func TestRequestSkipsNonJSONNoiseLines(t *testing.T) {
	f := transport.NewFake("I (123) app: boot", "", "not json", `{"ok":true,"count":1}`)
	reply, err := New(f).GetStatus()
	if err != nil || Int64(reply["count"]) != 1 {
		t.Fatalf("reply %v err %v", reply, err)
	}
}

func TestRequestRecoversJSONAfterNoisePrefix(t *testing.T) {
	f := transport.NewFake(`I (102) esp_image: segment 1: paddr=00010020 {"ok":true,"firmware":"0.1.0"}`)
	reply, err := New(f).Ping()
	if err != nil || reply["firmware"] != "0.1.0" {
		t.Fatalf("reply %v err %v", reply, err)
	}
}

func TestRequestRaisesOnErrorReply(t *testing.T) {
	f := transport.NewFake(`{"ok":false,"error":"unknown_cmd"}`)
	_, err := New(f).Request("nope", time.Second, nil)
	var de *DeviceError
	if !errors.As(err, &de) || de.Code != "unknown_cmd" {
		t.Fatalf("err %v", err)
	}
}

func TestRequestTimesOut(t *testing.T) {
	f := transport.NewFake()
	_, err := New(f).Request("ping", 20*time.Millisecond, nil)
	var de *DeviceError
	if !errors.As(err, &de) || de.Code != "timeout" {
		t.Fatalf("err %v", err)
	}
}

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

func TestOpenAndCloseDelegateToTransport(t *testing.T) {
	f := transport.NewFake()
	c := New(f)
	_ = c.Open()
	if !f.Opened {
		t.Fatal("not opened")
	}
	_ = c.Close()
	if f.Opened {
		t.Fatal("not closed")
	}
}

func page(total, from, to int) string {
	var recs []string
	for i := from; i < to; i++ {
		recs = append(recs, fmt.Sprintf("[%d,0,0,0,0,1]", i))
	}
	return fmt.Sprintf(`{"ok":true,"total":%d,"offset":%d,"records":[%s]}`, total, from, strings.Join(recs, ","))
}

func TestFetchLogPaginatesUntilTotal(t *testing.T) {
	f := transport.NewFake(page(1200, 0, 500), page(1200, 500, 1000), page(1200, 1000, 1200))
	records, total, err := New(f).FetchLog(0)
	if err != nil || total != 1200 || len(records) != 1200 {
		t.Fatalf("total %d len %d err %v", total, len(records), err)
	}
	var offsets []float64
	for _, s := range f.Sent {
		offsets = append(offsets, sentJSON(t, s)["offset"].(float64))
	}
	if fmt.Sprint(offsets) != "[0 500 1000]" {
		t.Fatalf("offsets %v", offsets)
	}
	if records[1199][0] != 1199 {
		t.Fatalf("last record %v", records[1199])
	}
}

func TestFetchLogFromOffset(t *testing.T) {
	f := transport.NewFake(page(1200, 1195, 1200))
	records, _, err := New(f).FetchLog(1195)
	if err != nil || len(records) != 5 {
		t.Fatalf("len %d err %v", len(records), err)
	}
	if got := sentJSON(t, f.Sent[0]); got["offset"] != float64(1195) || got["limit"] != float64(500) {
		t.Fatalf("sent %v", got)
	}
}

func TestNumberHelpers(t *testing.T) {
	if Int64(json.Number("42")) != 42 || Int64(float64(7)) != 7 || Int64("x") != 0 {
		t.Fatal("Int64")
	}
	if Float(json.Number("23.45")) != 23.45 || Float(nil) != 0 {
		t.Fatal("Float")
	}
	if !Bool(nil, true) || Bool(false, true) {
		t.Fatal("Bool")
	}
}
