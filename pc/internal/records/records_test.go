package records

import (
	"reflect"
	"testing"
	"time"
)

var pcTime = time.Date(2026, 9, 16, 15, 0, 0, 0, time.Local)
var sync = SyncInfo{BootID: 3, UptimeS: 1000, PCTime: pcTime}

func TestFromArrayAndUnits(t *testing.T) {
	r, err := FromArray([]int64{1789000000, -520, 4120, 101325, 1, 3})
	if err != nil {
		t.Fatal(err)
	}
	if r.Timestamp != 1789000000 || r.TempC() != -5.2 || r.HumPct() != 41.2 || r.PressureHPa() != 1013.25 || !r.TimeValid() {
		t.Fatalf("record %+v", r)
	}
	if _, err := FromArray([]int64{1, 2}); err == nil {
		t.Fatal("short array must fail")
	}
}

func TestResolveTimeValidUsesEpoch(t *testing.T) {
	r := LogRecord{Timestamp: 1789000000, Flags: 1, BootID: 3}
	got, ok := ResolveTime(r, sync)
	want := time.Unix(1789000000, 0)
	if !ok || !got.Equal(want) {
		t.Fatalf("got %v ok %v", got, ok)
	}
	if FormatTime(r, sync) != want.Format(TimeFormat) {
		t.Fatalf("format %q", FormatTime(r, sync))
	}
}

func TestResolveTimeSameBootIsEstimated(t *testing.T) {
	r := LogRecord{Timestamp: 400, BootID: 3}
	got, ok := ResolveTime(r, sync)
	if !ok || !got.Equal(pcTime.Add(-600*time.Second)) {
		t.Fatalf("got %v ok %v", got, ok)
	}
	if FormatTime(r, sync) != "~2026-09-16 14:50:00" {
		t.Fatalf("format %q", FormatTime(r, sync))
	}
}

func TestResolveTimeOtherBootIsUnknown(t *testing.T) {
	r := LogRecord{Timestamp: 3725, BootID: 2}
	if _, ok := ResolveTime(r, sync); ok {
		t.Fatal("should be unresolvable")
	}
	if FormatTime(r, sync) != "boot#2 +01:02:05" {
		t.Fatalf("format %q", FormatTime(r, sync))
	}
}

func TestCSVRow(t *testing.T) {
	if !reflect.DeepEqual(CSVHeader, []string{"timestamp", "temp_c", "hum_pct", "pressure_pa", "time_valid", "boot_id"}) {
		t.Fatalf("header %v", CSVHeader)
	}
	r := LogRecord{Timestamp: 1789000000, TempCenti: 2345, HumCenti: 4120, PressurePa: 101325, Flags: 1, BootID: 3}
	want := []string{time.Unix(1789000000, 0).Format("2006-01-02T15:04:05"), "23.45", "41.20", "101325", "1", "3"}
	if got := CSVRow(r, sync); !reflect.DeepEqual(got, want) {
		t.Fatalf("row %v", got)
	}
	unknown := LogRecord{Timestamp: 3725, TempCenti: 2345, HumCenti: 4120, PressurePa: 101325, BootID: 2}
	if got := CSVRow(unknown, sync); got[0] != "boot#2 +01:02:05" || got[4] != "0" {
		t.Fatalf("row %v", got)
	}
}
