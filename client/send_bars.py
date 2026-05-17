#!/usr/bin/env python3
"""
Send alternating vertical-bars to the Hitachi LM215 over HTTP.

POSTs the raw 30,720-byte framebuffer to http://<host>:<port>/frame,
toggling the bar phase by 8 columns each iteration to animate.

Usage:
    python3 send_bars.py --host 192.168.x.x [--port 80] [--width 10] [--delay 0.1]
"""
import argparse
import time
import urllib.request

# ── LCD geometry ──────────────────────────────────────────────────────────────
LCD_WIDTH     = 480
LCD_HEIGHT    = 128
LCD_ROWS      = 64
LCD_ROW_BYTES = 120
LCD_BUF_BYTES = LCD_ROWS * LCD_ROW_BYTES   # 7680 per phase
LCD_PHASES    = 4
LCD_TOTAL     = LCD_PHASES * LCD_BUF_BYTES  # 30720

# Temporal dither table: DITHER[level][phase] → 0|1
DITHER = [
    [0, 0, 0, 0],  # level 0 —  0%
    [1, 0, 0, 0],  # level 1 — 25%
    [1, 1, 1, 1],  # level 2 — 50%
    [1, 0, 1, 0],  # level 3 — 100%
]


def serialise(pixels: list) -> bytes:
    out = bytearray(LCD_TOTAL)
    for phase in range(LCD_PHASES):
        base = phase * LCD_BUF_BYTES
        for y in range(LCD_HEIGHT):
            for x in range(LCD_WIDTH):
                v = pixels[y * LCD_WIDTH + x]
                if not DITHER[v][phase]:
                    continue
                pulse    = x if x < 240 else x - 240
                row      = y % LCD_ROWS
                data_bit = (2 if x >= 240 else 0) | (1 if y >= 64 else 0)
                byte_idx = pulse >> 1
                bit_pos  = (4 + data_bit) if (pulse & 1) else data_bit
                out[base + row * LCD_ROW_BYTES + byte_idx] |= (1 << bit_pos)
    # invert for hardware polarity
    for i in range(len(out)):
        out[i] ^= 0xFF
    return bytes(out)


def bars_pixels(bar_width: int = 10, x_offset: int = 0) -> list:
    """Vertical bars cycling through levels 0-3, shifted by x_offset columns."""
    pixels = []
    for y in range(LCD_HEIGHT):
        for x in range(LCD_WIDTH):
            level = ((x + x_offset) // bar_width) % 4
            pixels.append(level)
    return pixels


def post_frame(url: str, payload: bytes):
    req = urllib.request.Request(
        url, data=payload, method='POST',
        headers={'Content-Type': 'application/octet-stream',
                 'Content-Length': str(len(payload))},
    )
    with urllib.request.urlopen(req, timeout=5) as resp:
        return resp.status, resp.read()


def main():
    p = argparse.ArgumentParser(description='Send alternating bars to LM215 LCD over HTTP')
    p.add_argument('--host',  required=True,  help='ESP32 IP address')
    p.add_argument('--port',  type=int, default=80)
    p.add_argument('--width', type=int, default=10, help='bar width in pixels')
    p.add_argument('--delay', type=float, default=0.1, help='seconds between frames')
    args = p.parse_args()

    url = f'http://{args.host}:{args.port}/frame'
    print(f'POSTing to {url} every {args.delay}s — Ctrl-C to stop')

    toggle = 0
    while True:
        offset = 8 if toggle else 0  # shift 8 columns to alternate bar position
        payload = serialise(bars_pixels(args.width, offset))
        try:
            status, body = post_frame(url, payload)
            print(f'[{status}] {body.decode(errors="replace").strip()}')
        except Exception as e:
            print(f'error: {e}')
        toggle ^= 1
        time.sleep(args.delay)


if __name__ == '__main__':
    main()
