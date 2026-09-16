from dataclasses import dataclass
from datetime import datetime, timedelta

FLAG_TIME_VALID = 0x01
TIME_FMT = "%Y-%m-%d %H:%M:%S"
CSV_HEADER = ["timestamp", "temp_c", "hum_pct", "pressure_pa", "time_valid", "boot_id"]


@dataclass(frozen=True)
class LogRecord:
    timestamp: int
    temp_centi: int
    hum_centi: int
    pressure_pa: int
    flags: int
    boot_id: int

    @classmethod
    def from_array(cls, a: list) -> "LogRecord":
        return cls(*a)

    @property
    def time_valid(self) -> bool:
        return bool(self.flags & FLAG_TIME_VALID)

    @property
    def temp_c(self) -> float:
        return self.temp_centi / 100

    @property
    def hum_pct(self) -> float:
        return self.hum_centi / 100

    @property
    def pressure_hpa(self) -> float:
        return self.pressure_pa / 100


@dataclass(frozen=True)
class SyncInfo:
    """Device clock state captured at connect time, used to place unsynced records on the PC timeline."""

    boot_id: int
    uptime_s: int
    pc_time: datetime


def resolve_time(rec: LogRecord, sync: SyncInfo) -> datetime | None:
    if rec.time_valid:
        return datetime.fromtimestamp(rec.timestamp)
    if rec.boot_id == sync.boot_id:
        return sync.pc_time - timedelta(seconds=sync.uptime_s - rec.timestamp)
    return None


def _relative_label(rec: LogRecord) -> str:
    h, rem = divmod(rec.timestamp, 3600)
    m, s = divmod(rem, 60)
    return f"boot#{rec.boot_id} +{h:02d}:{m:02d}:{s:02d}"


def format_time(rec: LogRecord, sync: SyncInfo) -> str:
    dt = resolve_time(rec, sync)
    if dt is None:
        return _relative_label(rec)
    prefix = "" if rec.time_valid else "~"
    return prefix + dt.strftime(TIME_FMT)


def csv_row(rec: LogRecord, sync: SyncInfo) -> list[str]:
    dt = resolve_time(rec, sync)
    when = dt.isoformat() if dt is not None else _relative_label(rec)
    return [when, f"{rec.temp_c:.2f}", f"{rec.hum_pct:.2f}", str(rec.pressure_pa), "1" if rec.time_valid else "0", str(rec.boot_id)]
