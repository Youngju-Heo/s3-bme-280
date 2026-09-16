# s3-bme-280

Waveshare ESP32-S3-Zero + BME280(SPI) 환경 로거. 장치가 주기적으로 온도·습도·기압을 측정해 플래시에 고정 크기(32,512건) 롤링 저장하고, PC에서 USB로 조회한다.

## 배선

| BME280 | ESP32-S3-Zero |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL | GPIO3 |
| SDA | GPIO4 |
| CSB | GPIO5 |
| SDO | GPIO6 |

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

USB Serial/JTAG 위 줄 단위 JSON. 예: `{"cmd":"get_log","offset":0,"limit":500}` → `{"ok":true,"total":N,"offset":0,"records":[[timestamp,temp_centi,hum_centi,pressure_pa,flags,boot_id],...]}`. 명령: `ping`, `set_time`, `read_now`, `get_status`, `get_log`, `clear_log`, `set_interval`. 상세는 `docs/feature/2026-09/2026-09-16-01-bme280-logger-design.md`.
