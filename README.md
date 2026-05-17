# hitachi-lcd-lm215

Firmware for driving a **Hitachi LM215** 480×128 passive LCD from an **ESP32 DevKit V1** over WiFi. Frames are sent via HTTP POST from a Python client on the local network. Includes a live clock, weather, and date display.

## Hardware

| | |
|---|---|
| **Display** | Hitachi LM215 — 480×128, 1-bit passive LCD, 4-quadrant dual-scan |
| **MCU** | ESP32 DevKit V1 (Xtensa LX6 dual-core, 240MHz, 520KB SRAM, 4MB Flash) |
| **Level shifter** | 74HCT245 octal buffer (3.3V → 5V) |

## Wiring

ESP32 GPIO pins connect to the LM215 via a 74HCT245 level-shifter. The header runs linearly: GPIO16–GPIO23 → LM215 pins 8→1.

| LM215 Pin | Signal | Function | ESP32 GPIO |
|---|---|---|---|
| 1 | D1 | Serial data — upper-left quadrant | GPIO 23 |
| 2 | D2 | Serial data — lower-left quadrant | GPIO 22 |
| 3 | FLM | Frame start | GPIO 21 |
| 4 | M | AC driving signal | GPIO 19 |
| 5 | CL1 | Data latch (row clock) | GPIO 18 |
| 6 | CL2 | Shift clock (pixel clock) | GPIO 5 |
| 7 | D3 | Serial data — upper-right quadrant | GPIO 17 |
| 8 | D4 | Serial data — lower-right quadrant | GPIO 16 |

## Architecture

**Core 1** owns the LCD refresh loop at maximum FreeRTOS priority. It cycles through 4 temporal-dither phases continuously, driving 64 rows × 240 CL2 pulses per pass. The full dither cycle runs at ~30Hz, producing four perceived brightness levels through pixel duty cycle.

**Core 0** runs the WiFi stack and HTTP server. When a complete frame arrives via `POST /frame`, it is written into the back buffer and atomically swapped — Core 1 picks it up on the next pass with no tearing.

## HTTP API

| Method | Path | Description |
|---|---|---|
| `POST` | `/frame` | Upload a full frame — 30720 bytes, `application/octet-stream` |
| `GET` | `/status` | Returns `{"ip":"…","uptime_ms":…,"frames":…}` |

### Frame format

30720 bytes = 4 dither phases × 64 rows × 120 bytes/row.

Each row is 240 CL2 pulses, nibble-packed 2 per byte (low nibble = even pulse, high nibble = odd). Bits 0–3 map to D1–D4. All bytes are XOR'd with `0xFF` for hardware polarity.

## Building & Flashing

Requires [PlatformIO](https://platformio.org/). The `espressif32` platform installs automatically on first build.

```bash
# Build
~/.platformio/penv/bin/platformio run -e esp32dev

# Flash (CP210x on /dev/ttyUSB0)
~/.platformio/penv/bin/platformio run -e esp32dev --target upload
```

The device registers on the network as `lm215.local` via mDNS.

## WiFi configuration

Copy `src/credentials.h.example` to `src/credentials.h` and fill in your network details:

```cpp
#define WIFI_SSID     "your-network-name"
#define WIFI_PASSWORD "your-password"
```

`src/credentials.h` is gitignored and never committed.

## Python clients

```bash
pip install -r client/requirements.txt
```

### Clock / weather (`client/clock.py`)

Displays a live 12-hour clock with AM/PM, current weather, and date across the full 480×128 panel:

- **Left 360px** — H:MM:SS in Moonhouse block font with AM/PM
- **Top-right 120×64** — weather condition + temperature
- **Bottom-right 120×64** — day name + date

Weather is provided by [Open-Meteo](https://open-meteo.com/) — free, no API key required.

**Font setup** — Moonhouse is not bundled (freeware, NimaType). Download from [fontspace.com/moonhouse-font-f18420](https://www.fontspace.com/moonhouse-font-f18420), extract the ZIP, and place `Moonhouse.ttf` in `client/fonts/`. The `fonts/` directory is gitignored.

**Configuration** — copy `client/.env.example` to `client/.env` and set your US zip code:

```
ZIP=10001
```

**Usage:**

```bash
python3 client/clock.py                      # lm215.local, zip from .env
python3 client/clock.py --zip 90210
python3 client/clock.py --units metric
python3 client/clock.py --host 192.168.1.42
```

### Bars test (`client/send_bars.py`)

Sends animated vertical-bar patterns to verify HTTP transport and LCD rendering:

```bash
python3 client/send_bars.py --host lm215.local
```
