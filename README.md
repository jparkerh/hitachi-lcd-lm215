# hitachi-lcd-lm215

Firmware for driving a **Hitachi LM215** 480×128 passive LCD from an **Adafruit Feather RP2040** over USB serial. Pairs with the [lcd-web-renderer](https://github.com/jparkerh/lcd-web-renderer) browser-based design tool.

## Hardware

| | |
|---|---|
| **Display** | Hitachi LM215 — 480×128, 1-bit passive LCD, 4-quadrant dual-scan |
| **MCU** | Adafruit Feather RP2040 (RP2040, 133MHz, 264KB SRAM, 8MB Flash) |

## Wiring

| LM215 Signal | Function | Feather GPIO | Header Pad |
|---|---|---|---|
| D1 | Serial data — upper-left quadrant | GPIO 0 | TX |
| D2 | Serial data — lower-left quadrant | GPIO 1 | RX |
| D3 | Serial data — upper-right quadrant | GPIO 2 | SDA |
| D4 | Serial data — lower-right quadrant | GPIO 3 | SCL |
| FLM | Frame start | GPIO 7 | D5 |
| M | AC driving signal | GPIO 8 | D6 |
| CL1 | Data latch (row clock) | GPIO 9 | D9 |
| CL2 | Shift clock (pixel clock) | GPIO 10 | D10 |

All pins driven at 12mA. The LM215 is a 5V panel; the 3.3V RP2040 outputs are generally sufficient with max drive strength.

## How it works

**Core 1** owns the LCD refresh loop entirely — zero contention with USB. It cycles through 4 temporal-dither phases continuously, driving 64 rows × 240 CL2 pulses per refresh pass. Each phase takes ~8ms; the full 4-phase dither cycle runs at ~30Hz, producing four levels of perceived brightness through pixel on/off duty cycle.

**Core 0** handles USB serial (CDC) and the UART bridge state machine, writing incoming frames into the back buffer. When a complete frame arrives, it atomically swaps the buffer pointer — Core 1 picks it up on the next pass with no tearing.

## Wire Protocol

Frames are sent from [lcd-web-renderer](https://github.com/jparkerh/lcd-web-renderer) via Web Serial API at 1 Mbaud (USB CDC).

### Display frame

```
[0xAB] [0xCF] [30720 bytes]
```

30720 bytes = 4 dither phases × 64 rows × 120 bytes/row.
Each row: 240 CL2 pulses, nibble-packed 2 per byte (low nibble = even pulse, high nibble = odd pulse).
Bits 0–3 of each nibble map to D1–D4 respectively.

### Enter bootloader

```
[0xAB] [0xBB]
```

Reboots the Feather into BOOTSEL (RPI-RP2 mass storage) mode for flashing — no physical button press required. The lcd-web-renderer **⬆ Flash Mode** button sends this automatically.

## Building & Flashing

```bash
# Build
pio run

# Flash manually (Feather in BOOTSEL mode)
cp .pio/build/adafruit_feather_rp2040/firmware.uf2 /path/to/RPI-RP2/

# Or use the Flash Mode button in lcd-web-renderer while connected
```

Requires [PlatformIO](https://platformio.org/) and the `maxgerhardt/platform-raspberrypi` platform (installed automatically on first build).

## Branches

| Branch | Description |
|---|---|
| `main` | RP2040 / Adafruit Feather — **active** |
| `legacy-mega` | Original Arduino Mega 2560 implementation (superseded) |
