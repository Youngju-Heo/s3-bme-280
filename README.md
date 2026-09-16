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
| 파랑 깜빡임 | WiFi 자격증명은 있으나 접속 중/실패 |

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

## PC 도구 (Go)

```powershell
cd pc
go build -o bme280-tool.exe ./cmd/bme280-tool
go test ./...

.\bme280-tool.exe [--port COM9] status
.\bme280-tool.exe now
.\bme280-tool.exe log [--last N] [--since 2026-09-16T14:00] [--csv out.csv]
.\bme280-tool.exe clear [--yes]
.\bme280-tool.exe interval [SECONDS]
.\bme280-tool.exe wifi <SSID> <PASSWORD>
.\bme280-tool.exe wifi --clear
```

의존성은 `go.bug.st/serial` 하나다. 실행 파일 하나로 배포되며 Python이 필요 없다.
출력은 영어다(콘솔 도구 규칙).

시간은 CLI 접속 시마다 PC 시각으로 동기화된다. 동기화 전에 저장된 레코드는 같은 부팅 세대면 `~` 접두로 추정 시각을, 다른 세대면 `boot#N +HH:MM:SS`로 표시한다.

## 웹 조회

`bme280-tool wifi <SSID> <PASSWORD>`로 공유기 정보를 저장하면 장치가 접속하고 `status`에 IP가 표시된다. 브라우저에서 `http://<IP>/`를 열면 현재값·장치 상태·이력 그래프(1시간/1일/1주)를 볼 수 있다. WiFi가 연결되면 NTP로 시각이 자동 동기화되며, NTP 동기화 전에는 페이지에서 브라우저 시각 또는 직접 입력한 시각으로 설정할 수 있다. 웹은 조회 전용이며 삭제·주기 변경·WiFi 설정은 USB CLI로만 한다. `GET /api?cmd=…`는 시리얼 프로토콜과 같은 JSON을 돌려준다(읽기 명령만, `set_time`은 POST).

## 프로토콜

USB Serial/JTAG 위 줄 단위 JSON. 포트 설정, 명령별 요청/응답 형식, 레코드 필드, 시각 해석 규칙, 타임아웃 권장값은 [docs/protocol.md](docs/protocol.md)를 참조한다.
