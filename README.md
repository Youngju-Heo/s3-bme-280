# s3-bme-280

Waveshare ESP32-S3-Zero + BME280(SPI) 환경 로거. 장치가 주기적으로 온도·습도·기압을 측정해 플래시에 고정 크기(32,512건) 롤링 저장하고, PC에서 USB로 조회한다.

## 배선

| BME280 | ESP32-S3-Zero |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL | GPIO12 |
| SDA | GPIO11 |
| CSB | GPIO10 |
| SDO | GPIO13 |

## 상태 LED (보드 내장 RGB, GPIO21)

| 표시 | 의미 |
|---|---|
| 녹색 고정 | 센서·저장소 정상, 시간 동기화 완료 |
| 녹색 깜빡임 (0.5초) | 정상이나 아직 PC 접속(시간 동기화) 전 |
| 빨강 | 센서 연결 오류 또는 플래시 저장 실패 (다음 측정 주기에 반영) |

## 펌웨어 (ESP-IDF v6.0.1)

```powershell
. "C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1"
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COM9 flash
```

호스트 단위 테스트(MinGW gcc + Unity):

```powershell
powershell -File firmware\test_host\run-tests.ps1
```

## PC 도구

```powershell
cd pc
pip install -e ".[dev]"
pytest -q

bme280-tool [--port COM9] status
bme280-tool now
bme280-tool log [--last N] [--since 2026-09-16T14:00] [--csv out.csv]
bme280-tool clear [--yes]
bme280-tool interval [SECONDS]
```

시간은 CLI 접속 시마다 PC 시각으로 동기화된다. 동기화 전에 저장된 레코드는 같은 부팅 세대면 `~` 접두로 추정 시각을, 다른 세대면 `boot#N +HH:MM:SS`로 표시한다.

## 프로토콜

USB Serial/JTAG 위 줄 단위 JSON. 포트 설정, 명령별 요청/응답 형식, 레코드 필드, 시각 해석 규칙, 타임아웃 권장값은 [docs/protocol.md](docs/protocol.md)를 참조한다.
