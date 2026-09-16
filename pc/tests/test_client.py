import json

import pytest

from bme280_tool.client import DeviceClient, DeviceError
from fake_transport import FakeTransport


def test_request_sends_json_line_and_parses_reply():
    t = FakeTransport(['{"ok":true,"firmware":"0.1.0"}'])
    c = DeviceClient(t)
    reply = c.request("ping")
    assert t.sent == ['{"cmd": "ping"}']
    assert reply["firmware"] == "0.1.0"


def test_request_includes_params_with_snake_case_keys():
    t = FakeTransport(['{"ok":true}'])
    DeviceClient(t).request("get_log", offset=10, limit=500)
    assert json.loads(t.sent[0]) == {"cmd": "get_log", "offset": 10, "limit": 500}


def test_request_skips_non_json_noise_lines():
    t = FakeTransport(["I (123) app: boot", "", "not json", '{"ok":true,"count":1}'])
    assert DeviceClient(t).request("get_status")["count"] == 1


def test_request_raises_on_error_reply():
    t = FakeTransport(['{"ok":false,"error":"unknown_cmd"}'])
    with pytest.raises(DeviceError, match="unknown_cmd"):
        DeviceClient(t).request("nope")


def test_request_raises_on_timeout():
    t = FakeTransport([])
    with pytest.raises(DeviceError, match="timeout"):
        DeviceClient(t).request("ping", timeout=0.05)


def test_context_manager_opens_and_closes_transport():
    t = FakeTransport()
    with DeviceClient(t) as c:
        assert t.opened
        assert isinstance(c, DeviceClient)
    assert not t.opened


def test_fetch_log_paginates_until_total():
    page1 = {"ok": True, "total": 1200, "offset": 0, "records": [[i, 0, 0, 0, 0, 1] for i in range(500)]}
    page2 = {"ok": True, "total": 1200, "offset": 500, "records": [[i, 0, 0, 0, 0, 1] for i in range(500, 1000)]}
    page3 = {"ok": True, "total": 1200, "offset": 1000, "records": [[i, 0, 0, 0, 0, 1] for i in range(1000, 1200)]}
    t = FakeTransport([json.dumps(page1), json.dumps(page2), json.dumps(page3)])
    records, total = DeviceClient(t).fetch_log()
    assert total == 1200
    assert len(records) == 1200
    assert [json.loads(s)["offset"] for s in t.sent] == [0, 500, 1000]


def test_fetch_log_from_offset():
    page = {"ok": True, "total": 1200, "offset": 1195, "records": [[i, 0, 0, 0, 0, 1] for i in range(1195, 1200)]}
    t = FakeTransport([json.dumps(page)])
    records, total = DeviceClient(t).fetch_log(offset=1195)
    assert len(records) == 5
    assert json.loads(t.sent[0]) == {"cmd": "get_log", "offset": 1195, "limit": 500}
