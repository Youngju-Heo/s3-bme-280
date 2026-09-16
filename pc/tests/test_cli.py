import json
from datetime import datetime

import pytest

from bme280_tool.cli import main
from bme280_tool.client import DeviceClient
from fake_transport import FakeTransport

PING = '{"ok":true,"firmware":"0.1.0","boot_id":3,"uptime_s":1000,"time_valid":false}'
SET_TIME = '{"ok":true,"boot_id":3,"uptime_s":1000}'


def make_factory(responses: list[str]):
    transport = FakeTransport([PING, SET_TIME, *responses])

    def factory(port: str) -> DeviceClient:
        assert port == "COM9"
        return DeviceClient(transport)

    return factory, transport


def test_connect_syncs_time_first():
    factory, t = make_factory(['{"ok":true,"temp_c":23.45,"hum_pct":41.20,"pressure_pa":101325}'])
    assert main(["--port", "COM9", "now"], client_factory=factory) == 0
    assert json.loads(t.sent[0]) == {"cmd": "ping"}
    sync = json.loads(t.sent[1])
    assert sync["cmd"] == "set_time"
    assert abs(sync["epoch"] - datetime.now().timestamp()) < 5


def test_now_prints_reading(capsys):
    factory, _ = make_factory(['{"ok":true,"temp_c":23.45,"hum_pct":41.20,"pressure_pa":101325}'])
    main(["--port", "COM9", "now"], client_factory=factory)
    out = capsys.readouterr().out
    assert "온도" in out and "23.45" in out
    assert "습도" in out and "41.20" in out
    assert "기압" in out and "1013.25" in out


def test_status_prints_fields(capsys):
    factory, _ = make_factory(['{"ok":true,"count":1234,"capacity":32512,"interval_s":60,"sensor_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121}'])
    main(["--port", "COM9", "status"], client_factory=factory)
    out = capsys.readouterr().out
    assert "1234 / 32512" in out
    assert "60초" in out
    assert "정상" in out


def test_log_table_newest_first(capsys):
    records = [[1789000000 + i * 60, 2000 + i, 5000, 101300, 1, 3] for i in range(3)]
    factory, _ = make_factory([json.dumps({"ok": True, "total": 3, "offset": 0, "records": records})])
    main(["--port", "COM9", "log"], client_factory=factory)
    lines = [l for l in capsys.readouterr().out.splitlines() if l.strip()]
    assert "20.02" in lines[1]   # newest (index 2) first, after header line
    assert "20.00" in lines[3]


def test_log_last_n_fetches_tail_only(capsys):
    records = [[1789000000 + i, 2000, 5000, 101300, 1, 3] for i in range(1195, 1200)]
    factory, t = make_factory(['{"ok":true,"count":1200,"capacity":32512,"interval_s":60,"sensor_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121}',
                              json.dumps({"ok": True, "total": 1200, "offset": 1195, "records": records})])
    main(["--port", "COM9", "log", "--last", "5"], client_factory=factory)
    assert json.loads(t.sent[3]) == {"cmd": "get_log", "offset": 1195, "limit": 500}
    assert len([l for l in capsys.readouterr().out.splitlines() if l.strip()]) == 6


def test_log_since_filters(capsys):
    base = int(datetime(2026, 9, 16, 12, 0, 0).timestamp())
    records = [[base + i * 3600, 2000, 5000, 101300, 1, 3] for i in range(4)]   # 12:00 .. 15:00
    factory, _ = make_factory([json.dumps({"ok": True, "total": 4, "offset": 0, "records": records})])
    main(["--port", "COM9", "log", "--since", "2026-09-16T14:00"], client_factory=factory)
    lines = [l for l in capsys.readouterr().out.splitlines() if l.strip()]
    assert len(lines) == 3   # header + 14:00 + 15:00


def test_log_csv_writes_file(tmp_path, capsys):
    records = [[1789000000, 2345, 4120, 101325, 1, 3], [700, 2000, 5000, 101300, 0, 3]]
    factory, _ = make_factory([json.dumps({"ok": True, "total": 2, "offset": 0, "records": records})])
    out = tmp_path / "out.csv"
    main(["--port", "COM9", "log", "--csv", str(out)], client_factory=factory)
    text = out.read_text(encoding="utf-8").splitlines()
    assert text[0] == "timestamp,temp_c,hum_pct,pressure_pa,time_valid,boot_id"
    assert len(text) == 3
    assert "2건" in capsys.readouterr().out


def test_clear_requires_confirmation(monkeypatch, capsys):
    factory, t = make_factory(['{"ok":true}'])
    monkeypatch.setattr("builtins.input", lambda _: "n")
    assert main(["--port", "COM9", "clear"], client_factory=factory) == 0
    assert not any(json.loads(s)["cmd"] == "clear_log" for s in t.sent)
    assert "취소" in capsys.readouterr().out


def test_clear_with_yes(capsys):
    factory, t = make_factory(['{"ok":true}'])
    assert main(["--port", "COM9", "clear", "--yes"], client_factory=factory) == 0
    assert json.loads(t.sent[2]) == {"cmd": "clear_log"}
    assert "삭제" in capsys.readouterr().out


def test_interval_get_and_set(capsys):
    factory, _ = make_factory(['{"ok":true,"count":0,"capacity":32512,"interval_s":60,"sensor_ok":true,"time_valid":true,"boot_id":3,"uptime_s":121}'])
    main(["--port", "COM9", "interval"], client_factory=factory)
    assert "60초" in capsys.readouterr().out

    factory, t = make_factory(['{"ok":true}'])
    assert main(["--port", "COM9", "interval", "120"], client_factory=factory) == 0
    assert json.loads(t.sent[2]) == {"cmd": "set_interval", "interval_s": 120}
    assert "120초" in capsys.readouterr().out


def test_device_error_is_reported(capsys):
    factory, _ = make_factory(['{"ok":false,"error":"sensor_error"}'])
    assert main(["--port", "COM9", "now"], client_factory=factory) == 1
    assert "sensor_error" in capsys.readouterr().err
