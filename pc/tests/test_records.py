from datetime import datetime, timedelta

import pytest

from bme280_tool.records import CSV_HEADER, LogRecord, SyncInfo, altitude_m, csv_row, format_time, resolve_time

PC_TIME = datetime(2026, 9, 16, 15, 0, 0)
SYNC = SyncInfo(boot_id=3, uptime_s=1000, pc_time=PC_TIME)


def test_from_array_and_units():
    r = LogRecord.from_array([1789000000, -520, 4120, 101325, 1, 3])
    assert r.timestamp == 1789000000
    assert r.temp_c == -5.2
    assert r.hum_pct == 41.2
    assert r.pressure_hpa == 1013.25
    assert r.time_valid is True


def test_resolve_time_valid_uses_epoch():
    r = LogRecord(1789000000, 0, 0, 0, 1, 3)
    assert resolve_time(r, SYNC) == datetime.fromtimestamp(1789000000)
    assert format_time(r, SYNC) == datetime.fromtimestamp(1789000000).strftime("%Y-%m-%d %H:%M:%S")


def test_resolve_time_same_boot_is_estimated():
    r = LogRecord(400, 0, 0, 0, 0, 3)   # 400 s after boot; device uptime is 1000 s now
    assert resolve_time(r, SYNC) == PC_TIME - timedelta(seconds=600)
    assert format_time(r, SYNC) == "~2026-09-16 14:50:00"


def test_resolve_time_other_boot_is_unknown():
    r = LogRecord(3725, 0, 0, 0, 0, 2)
    assert resolve_time(r, SYNC) is None
    assert format_time(r, SYNC) == "boot#2 +01:02:05"


def test_csv_row():
    r = LogRecord(1789000000, 2345, 4120, 101325, 1, 3)
    assert CSV_HEADER == ["timestamp", "temp_c", "hum_pct", "pressure_pa", "altitude_m", "time_valid", "boot_id"]
    assert csv_row(r, SYNC) == [datetime.fromtimestamp(1789000000).isoformat(), "23.45", "41.20", "101325", "0.0", "1", "3"]
    assert csv_row(r, SYNC, sea_level_hpa=1023.25)[4] == "82.8"
    unknown = LogRecord(3725, 2345, 4120, 101325, 0, 2)
    assert csv_row(unknown, SYNC)[0] == "boot#2 +01:02:05"


def test_altitude_from_pressure():
    assert altitude_m(101325) == pytest.approx(0.0, abs=0.01)
    assert altitude_m(100000) == pytest.approx(110.9, abs=0.05)
    assert altitude_m(101325, sea_level_hpa=1023.25) == pytest.approx(82.8, abs=0.05)
