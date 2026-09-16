from collections import deque


class FakeTransport:
    """Scripted transport: queued response lines, records every sent line."""

    def __init__(self, responses: list[str] | None = None) -> None:
        self.responses: deque[str] = deque(responses or [])
        self.sent: list[str] = []
        self.opened = False

    def open(self) -> None:
        self.opened = True

    def close(self) -> None:
        self.opened = False

    def send_line(self, line: str) -> None:
        self.sent.append(line)

    def read_line(self) -> str | None:
        return self.responses.popleft() if self.responses else None
