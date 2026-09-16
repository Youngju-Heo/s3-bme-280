import json
import time


class DeviceError(Exception):
    """Raised when the device replies with an error or does not reply."""


class DeviceClient:
    PAGE_LIMIT = 500

    def __init__(self, transport) -> None:
        self._t = transport

    def __enter__(self) -> "DeviceClient":
        self._t.open()
        return self

    def __exit__(self, *exc) -> None:
        self._t.close()

    def request(self, cmd: str, timeout: float = 3.0, **params) -> dict:
        self._t.send_line(json.dumps({"cmd": cmd, **params}))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self._t.read_line()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(msg, dict) or "ok" not in msg:
                continue
            if not msg["ok"]:
                raise DeviceError(msg.get("error", "unknown"))
            return msg
        raise DeviceError("timeout")

    def ping(self) -> dict:
        return self.request("ping")

    def set_time(self, epoch: int) -> dict:
        return self.request("set_time", epoch=epoch)

    def read_now(self) -> dict:
        return self.request("read_now")

    def get_status(self) -> dict:
        return self.request("get_status")

    def get_log(self, offset: int, limit: int) -> dict:
        return self.request("get_log", timeout=10.0, offset=offset, limit=limit)

    def clear_log(self) -> dict:
        return self.request("clear_log", timeout=30.0)

    def set_interval(self, seconds: int) -> dict:
        return self.request("set_interval", interval_s=seconds)

    def fetch_log(self, offset: int = 0) -> tuple[list[list], int]:
        records: list[list] = []
        page = self.get_log(offset, self.PAGE_LIMIT)
        total = page["total"]
        records.extend(page["records"])
        offset += self.PAGE_LIMIT
        while offset < total:
            page = self.get_log(offset, self.PAGE_LIMIT)
            records.extend(page["records"])
            offset += self.PAGE_LIMIT
        return records, total
