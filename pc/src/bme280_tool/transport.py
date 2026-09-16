import serial


class SerialTransport:
    """Line-oriented serial link. DTR/RTS are held low so opening the port does not reset the ESP32-S3."""

    def __init__(self, port: str, timeout: float = 3.0) -> None:
        self._ser = serial.Serial()
        self._ser.port = port
        self._ser.baudrate = 115200
        self._ser.timeout = timeout
        self._ser.dtr = False
        self._ser.rts = False

    def open(self) -> None:
        self._ser.open()
        self._ser.reset_input_buffer()

    def close(self) -> None:
        if self._ser.is_open:
            self._ser.close()

    def send_line(self, line: str) -> None:
        self._ser.write((line + "\n").encode("utf-8"))

    def read_line(self) -> str | None:
        raw = self._ser.readline()
        if not raw:
            return None
        return raw.decode("utf-8", errors="replace").strip()
