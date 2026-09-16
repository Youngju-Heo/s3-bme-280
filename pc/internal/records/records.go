// Package records decodes device log records and places them on the PC timeline.
package records

import (
	"fmt"
	"strconv"
	"time"
)

const (
	FlagTimeValid = 0x01
	TimeFormat    = "2006-01-02 15:04:05"
	csvTimeFormat = "2006-01-02T15:04:05"
)

var CSVHeader = []string{"timestamp", "temp_c", "hum_pct", "pressure_pa", "time_valid", "boot_id"}

type LogRecord struct {
	Timestamp  int64 // epoch seconds if TimeValid, else seconds since boot
	TempCenti  int
	HumCenti   int
	PressurePa int64
	Flags      int
	BootID     int
}

// FromArray decodes the protocol's [timestamp, temp_centi, hum_centi, pressure_pa, flags, boot_id].
func FromArray(a []int64) (LogRecord, error) {
	if len(a) != 6 {
		return LogRecord{}, fmt.Errorf("record has %d fields, want 6", len(a))
	}
	return LogRecord{Timestamp: a[0], TempCenti: int(a[1]), HumCenti: int(a[2]), PressurePa: a[3], Flags: int(a[4]), BootID: int(a[5])}, nil
}

func (r LogRecord) TimeValid() bool      { return r.Flags&FlagTimeValid != 0 }
func (r LogRecord) TempC() float64       { return float64(r.TempCenti) / 100 }
func (r LogRecord) HumPct() float64      { return float64(r.HumCenti) / 100 }
func (r LogRecord) PressureHPa() float64 { return float64(r.PressurePa) / 100 }

// SyncInfo is the device clock state captured at connect time.
type SyncInfo struct {
	BootID  int
	UptimeS int64
	PCTime  time.Time
}

// ResolveTime returns the record's wall-clock time when it is known (synced) or estimable
// (same boot as the current connection); ok is false for records from earlier boots.
func ResolveTime(r LogRecord, s SyncInfo) (time.Time, bool) {
	if r.TimeValid() {
		return time.Unix(r.Timestamp, 0), true
	}
	if r.BootID == s.BootID {
		return s.PCTime.Add(-time.Duration(s.UptimeS-r.Timestamp) * time.Second), true
	}
	return time.Time{}, false
}

func relativeLabel(r LogRecord) string {
	h := r.Timestamp / 3600
	m := (r.Timestamp % 3600) / 60
	sec := r.Timestamp % 60
	return fmt.Sprintf("boot#%d +%02d:%02d:%02d", r.BootID, h, m, sec)
}

func FormatTime(r LogRecord, s SyncInfo) string {
	t, ok := ResolveTime(r, s)
	if !ok {
		return relativeLabel(r)
	}
	if r.TimeValid() {
		return t.Format(TimeFormat)
	}
	return "~" + t.Format(TimeFormat)
}

func CSVRow(r LogRecord, s SyncInfo) []string {
	when := relativeLabel(r)
	if t, ok := ResolveTime(r, s); ok {
		when = t.Format(csvTimeFormat)
	}
	valid := "0"
	if r.TimeValid() {
		valid = "1"
	}
	return []string{when, fmt.Sprintf("%.2f", r.TempC()), fmt.Sprintf("%.2f", r.HumPct()),
		strconv.FormatInt(r.PressurePa, 10), valid, strconv.Itoa(r.BootID)}
}
