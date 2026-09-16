import argparse
import csv
import sys
import time
from datetime import datetime

import serial

from .client import DeviceClient, DeviceError
from .records import CSV_HEADER, LogRecord, SyncInfo, csv_row, format_time, resolve_time
from .transport import SerialTransport

DEFAULT_PORT = "COM9"


def _open_client(port: str) -> DeviceClient:
    return DeviceClient(SerialTransport(port))


def _connect(client: DeviceClient) -> SyncInfo:
    client.ping()
    reply = client.set_time(int(time.time()))
    return SyncInfo(boot_id=reply["boot_id"], uptime_s=reply["uptime_s"], pc_time=datetime.now())


def _hms(seconds: int) -> str:
    h, rem = divmod(seconds, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


def cmd_status(client: DeviceClient, sync: SyncInfo, args) -> int:
    s = client.get_status()
    print(f"저장 레코드: {s['count']} / {s['capacity']}")
    print(f"측정 주기: {s['interval_s']}초")
    print(f"센서 상태: {'정상' if s['sensor_ok'] else '오류'}")
    print(f"저장소 상태: {'정상' if s.get('store_ok', True) else '오류'}")
    print(f"시간 동기화: {'완료' if s['time_valid'] else '미완료'}")
    print(f"부팅 세대: {s['boot_id']}, 가동 시간: {_hms(s['uptime_s'])}")
    return 0


def cmd_now(client: DeviceClient, sync: SyncInfo, args) -> int:
    r = client.read_now()
    print(f"온도: {r['temp_c']:.2f} °C")
    print(f"습도: {r['hum_pct']:.2f} %")
    print(f"기압: {r['pressure_pa'] / 100:.2f} hPa")
    return 0


def _fetch_records(client: DeviceClient, last: int | None) -> list[LogRecord]:
    offset = 0
    if last is not None:
        total = client.get_status()["count"]
        offset = max(0, total - last)
    raw, _ = client.fetch_log(offset)
    return [LogRecord.from_array(a) for a in raw]


def cmd_log(client: DeviceClient, sync: SyncInfo, args) -> int:
    records = _fetch_records(client, args.last)
    if args.since is not None:
        since = datetime.fromisoformat(args.since)
        records = [r for r in records if (dt := resolve_time(r, sync)) is not None and dt >= since]
    records.reverse()   # newest first

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(CSV_HEADER)
            for r in records:
                writer.writerow(csv_row(r, sync))
        print(f"{len(records)}건을 {args.csv}에 저장했습니다")
        return 0

    print(f"{'시각':<22}{'온도(°C)':>10}{'습도(%)':>10}{'기압(hPa)':>12}")
    for r in records:
        print(f"{format_time(r, sync):<22}{r.temp_c:>10.2f}{r.hum_pct:>10.2f}{r.pressure_hpa:>12.2f}")
    return 0


def cmd_clear(client: DeviceClient, sync: SyncInfo, args) -> int:
    if not args.yes:
        answer = input("장치의 이력을 모두 삭제합니다. 계속할까요? (y/N): ")
        if answer.strip().lower() != "y":
            print("취소했습니다")
            return 0
    client.clear_log()
    print("이력을 삭제했습니다")
    return 0


def cmd_interval(client: DeviceClient, sync: SyncInfo, args) -> int:
    if args.seconds is None:
        print(f"측정 주기: {client.get_status()['interval_s']}초")
        return 0
    client.set_interval(args.seconds)
    print(f"측정 주기를 {args.seconds}초로 설정했습니다")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="bme280-tool", description="ESP32-S3 BME280 로거 조회 도구")
    p.add_argument("--port", default=DEFAULT_PORT, help=f"시리얼 포트 (기본 {DEFAULT_PORT})")
    sub = p.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="장치 상태").set_defaults(func=cmd_status)
    sub.add_parser("now", help="즉시 측정값").set_defaults(func=cmd_now)

    log = sub.add_parser("log", help="이력 조회")
    log.add_argument("--last", type=int, help="최근 N건만")
    log.add_argument("--since", help="이 시각 이후만 (ISO 8601, 예: 2026-09-16T14:00)")
    log.add_argument("--csv", help="표 대신 CSV 파일로 저장")
    log.set_defaults(func=cmd_log)

    clear = sub.add_parser("clear", help="이력 삭제")
    clear.add_argument("--yes", action="store_true", help="확인 없이 삭제")
    clear.set_defaults(func=cmd_clear)

    interval = sub.add_parser("interval", help="측정 주기 조회/변경")
    interval.add_argument("seconds", type=int, nargs="?", help="새 주기(초, 10~3600)")
    interval.set_defaults(func=cmd_interval)
    return p


def main(argv: list[str] | None = None, client_factory=None) -> int:
    args = build_parser().parse_args(argv)
    factory = client_factory or _open_client
    try:
        with factory(args.port) as client:
            sync = _connect(client)
            return args.func(client, sync, args)
    except DeviceError as e:
        print(f"오류: 장치 응답 실패 ({e})", file=sys.stderr)
        return 1
    except serial.SerialException as e:
        print(f"오류: 포트를 열 수 없습니다 ({e})", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
