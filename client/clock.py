#!/usr/bin/env python3
"""
clock.py — Live clock, weather, and date on the Hitachi LM215 480×128 LCD.

Layout (480×128 px):
  Left 360px : HH:MM:SS in 7-segment block font, full height
  Top-right  : weather condition + temperature  (120×64)
  Bot-right  : day name + date                  (120×64)

Usage:
    python3 clock.py --api-key YOUR_OWM_KEY --city "New York"
    python3 clock.py --api-key YOUR_OWM_KEY --lat 40.71 --lon -74.01 --units metric
"""

import argparse
import datetime
import json
import os
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from dotenv import load_dotenv
from PIL import Image, ImageDraw, ImageFont

load_dotenv(Path(__file__).parent / '.env')

# ── LCD geometry + serialiser ─────────────────────────────────────────────────
LCD_WIDTH     = 480
LCD_HEIGHT    = 128
LCD_ROWS      = 64
LCD_ROW_BYTES = 120
LCD_BUF_BYTES = LCD_ROWS * LCD_ROW_BYTES
LCD_PHASES    = 4
LCD_TOTAL     = LCD_PHASES * LCD_BUF_BYTES   # 30720

DITHER = [
    [0, 0, 0, 0],  # level 0 —  off
    [1, 0, 0, 0],  # level 1 — 25%
    [1, 1, 1, 1],  # level 2 — full on
    [1, 0, 1, 0],  # level 3 — 50%
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
    for i in range(len(out)):
        out[i] ^= 0xFF
    return bytes(out)


def post_frame(url: str, payload: bytes) -> int:
    req = urllib.request.Request(
        url, data=payload, method='POST',
        headers={'Content-Type': 'application/octet-stream',
                 'Content-Length': str(len(payload))},
    )
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status


# ── Pixel buffer helpers ──────────────────────────────────────────────────────
def blank() -> list:
    return [0] * (LCD_WIDTH * LCD_HEIGHT)


def fill_rect(buf, x: int, y: int, w: int, h: int, level: int = 2):
    for py in range(max(0, y), min(LCD_HEIGHT, y + h)):
        row_off = py * LCD_WIDTH
        for px in range(max(0, x), min(LCD_WIDTH, x + w)):
            buf[row_off + px] = level


# ── Moonhouse font clock ──────────────────────────────────────────────────────
CLOCK_W    = 360
_FONT_PATH = Path(__file__).parent / 'fonts' / 'Moonhouse.ttf'

_clock_layout = None   # (font, cell_w, col_w, x0)


def _get_clock_layout():
    """
    Find the largest font size where a monospace HH:MM:SS layout fits inside
    CLOCK_W/1.15 × LCD_HEIGHT/1.15, then scale up 15% for the final render.
    Each digit is placed in a fixed cell_w column so the clock never shifts.
    """
    global _clock_layout
    if _clock_layout is not None:
        return _clock_layout

    target_w = CLOCK_W / 1.15
    target_h = LCD_HEIGHT / 1.15

    for size in range(140, 10, -2):
        f     = ImageFont.truetype(str(_FONT_PATH), size)
        probe = Image.new('L', (4000, 400), 0)
        draw  = ImageDraw.Draw(probe)

        def w(ch):
            bb = draw.textbbox((0, 0), ch, font=f)
            return bb[2] - bb[0]

        cell_w = max(w(d) for d in '0123456789')
        col_w  = max(w(':'), cell_w // 3)
        dig_h  = draw.textbbox((0, 0), '0', font=f)[3] - draw.textbbox((0, 0), '0', font=f)[1]

        if 6 * cell_w + 2 * col_w <= target_w and dig_h <= target_h:
            final_size = int(size * 1.15)
            f2    = ImageFont.truetype(str(_FONT_PATH), final_size)
            probe2 = Image.new('L', (4000, 400), 0)
            draw2  = ImageDraw.Draw(probe2)

            def w2(ch):
                bb = draw2.textbbox((0, 0), ch, font=f2)
                return bb[2] - bb[0]

            cell_w2 = max(w2(d) for d in '0123456789')
            col_w2  = max(w2(':'), cell_w2 // 3)
            total_w = 6 * cell_w2 + 2 * col_w2
            x0      = (CLOCK_W - total_w) // 2

            print(f'Clock: {size}pt → {final_size}pt (×1.15), '
                  f'cell={cell_w2}px, total={total_w}px, x0={x0}')
            _clock_layout = (f2, cell_w2, col_w2, x0)
            return _clock_layout

    f = ImageFont.truetype(str(_FONT_PATH), 12)
    _clock_layout = (f, 10, 5, 0)
    return _clock_layout


def draw_clock(buf, now: datetime.datetime):
    font, cell_w, col_w, x0 = _get_clock_layout()
    hour12 = now.hour % 12 or 12
    hh = f'{hour12:02d}'
    mm = f'{now.minute:02d}'
    ss = f'{now.second:02d}'
    ampm = 'AM' if now.hour < 12 else 'PM'

    cy = LCD_HEIGHT // 2  # vertical centre for the main digits

    # Fixed slot positions — never move regardless of which digits show
    slots = [
        (x0,                          cell_w, hh[0]),
        (x0 +   cell_w,               cell_w, hh[1]),
        (x0 + 2*cell_w,               col_w,  ':' ),
        (x0 + 2*cell_w +   col_w,     cell_w, mm[0]),
        (x0 + 3*cell_w +   col_w,     cell_w, mm[1]),
        (x0 + 4*cell_w +   col_w,     col_w,  ':' ),
        (x0 + 4*cell_w + 2*col_w,     cell_w, ss[0]),
        (x0 + 5*cell_w + 2*col_w,     cell_w, ss[1]),
    ]

    img  = Image.new('L', (CLOCK_W, LCD_HEIGHT), 0)
    draw = ImageDraw.Draw(img)
    for slot_x, slot_w, ch in slots:
        bb   = draw.textbbox((0, 0), ch, font=font)
        cw   = bb[2] - bb[0]
        ch_h = bb[3] - bb[1]
        tx   = slot_x + (slot_w - cw) // 2 - bb[0]
        ty   = cy - ch_h // 2 - bb[1]
        draw.text((tx, ty), ch, fill=255, font=font)

    # AM/PM centred under the SS digits, 10px below the digit bottom
    dig_bb  = draw.textbbox((0, 0), '0', font=font)
    dig_bot = cy + (dig_bb[3] - dig_bb[1]) // 2  # pixel row of digit bottom
    ss_x    = x0 + 4*cell_w + 2*col_w
    ss_w    = 2 * cell_w
    ampm_f  = _font(24)
    ab      = draw.textbbox((0, 0), ampm, font=ampm_f)
    draw.text((ss_x + (ss_w - (ab[2] - ab[0])) // 2 - ab[0],
               dig_bot + 10 - ab[1]),
              ampm, fill=255, font=ampm_f)

    pix = img.load()
    for iy in range(LCD_HEIGHT):
        for ix in range(CLOCK_W):
            if pix[ix, iy] > 64:
                buf[iy * LCD_WIDTH + ix] = 2


# ── Right-panel text via Pillow ───────────────────────────────────────────────
PANEL_X   = CLOCK_W          # 360 — divider column
PANEL_MID = LCD_HEIGHT // 2  # 64  — weather / date divider row

_font_cache: dict = {}


def _font(size: int) -> ImageFont.FreeTypeFont:
    if size not in _font_cache:
        candidates = [
            '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf',
            '/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf',
            '/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf',
            '/usr/share/fonts/truetype/freefont/FreeMonoBold.ttf',
        ]
        for p in candidates:
            try:
                _font_cache[size] = ImageFont.truetype(p, size)
                break
            except (IOError, OSError):
                pass
        else:
            _font_cache[size] = ImageFont.load_default()
    return _font_cache[size]


def _blit_centered(buf, text: str, x: int, y: int, w: int, h: int, font):
    """Render text centred in box (x, y, w, h) into the pixel buffer."""
    img  = Image.new('L', (w, h), 0)
    draw = ImageDraw.Draw(img)
    bb   = draw.textbbox((0, 0), text, font=font)
    tw   = bb[2] - bb[0]
    th   = bb[3] - bb[1]
    tx   = max(0, (w - tw) // 2) - bb[0]
    ty   = max(0, (h - th) // 2) - bb[1]
    draw.text((tx, ty), text, fill=255, font=font)
    pix = img.load()
    for iy in range(h):
        for ix in range(w):
            if pix[ix, iy] > 64:
                bx, by = x + ix, y + iy
                if 0 <= bx < LCD_WIDTH and 0 <= by < LCD_HEIGHT:
                    buf[by * LCD_WIDTH + bx] = 2


def draw_panel(buf, now: datetime.datetime, weather: dict):
    pw = LCD_WIDTH - PANEL_X - 1   # 119px usable width
    px = PANEL_X + 1

    # Dividers
    fill_rect(buf, PANEL_X,  0,        1, LCD_HEIGHT)                # vertical
    fill_rect(buf, PANEL_X,  PANEL_MID, LCD_WIDTH - PANEL_X, 1)     # horizontal

    half_h = PANEL_MID - 1   # 63px per half

    # Weather (top-right)
    cond = weather.get('cond', 'LOADING')
    temp = weather.get('temp', '---')
    _blit_centered(buf, cond, px, 1,               pw, half_h // 2,            _font(13))
    _blit_centered(buf, temp, px, half_h // 2 + 1, pw, half_h - half_h // 2,   _font(26))

    # Date (bottom-right)
    day  = now.strftime('%A').upper()
    date = now.strftime('%-d %b %Y').upper()
    _blit_centered(buf, day,  px, PANEL_MID + 1,               pw, half_h // 2,            _font(13))
    _blit_centered(buf, date, px, PANEL_MID + 1 + half_h // 2, pw, half_h - half_h // 2,   _font(16))


# ── Weather (Open-Meteo — free, no API key) ───────────────────────────────────
# WMO weather interpretation codes → short label
_WMO = {
    0: 'CLEAR',     1: 'CLEAR',      2: 'P.CLOUDY',  3: 'OVERCAST',
    45: 'FOG',      48: 'FOG',
    51: 'DRIZZLE',  53: 'DRIZZLE',   55: 'DRIZZLE',
    61: 'RAIN',     63: 'RAIN',      65: 'HVY RAIN',
    71: 'SNOW',     73: 'SNOW',      75: 'HVY SNOW',  77: 'SNOW',
    80: 'SHOWERS',  81: 'SHOWERS',   82: 'SHOWERS',
    85: 'SNOW SHR', 86: 'SNOW SHR',
    95: 'STORM',    96: 'STORM',     99: 'STORM',
}

_weather      = {'temp': '---', 'cond': 'LOADING'}
_weather_lock = threading.Lock()


def _zip_to_latlon(zipcode: str) -> tuple:
    url = f'https://api.zippopotam.us/us/{urllib.parse.quote(zipcode)}'
    with urllib.request.urlopen(url, timeout=10) as r:
        d = json.loads(r.read())
    return d['places'][0]['latitude'], d['places'][0]['longitude']


def _fetch_weather(zipcode: str, units: str):
    sym = '°F' if units == 'imperial' else '°C'
    unit_param = 'fahrenheit' if units == 'imperial' else 'celsius'
    lat, lon = _zip_to_latlon(zipcode)
    url = (f'https://api.open-meteo.com/v1/forecast'
           f'?latitude={lat}&longitude={lon}'
           f'&current=temperature_2m,weather_code'
           f'&temperature_unit={unit_param}&forecast_days=1')
    with urllib.request.urlopen(url, timeout=10) as r:
        data = json.loads(r.read())
    temp = f"{data['current']['temperature_2m']:.0f}{sym}"
    cond = _WMO.get(data['current']['weather_code'], 'UNKNOWN')
    return temp, cond


def _weather_thread(zipcode, units):
    while True:
        try:
            temp, cond = _fetch_weather(zipcode, units)
            with _weather_lock:
                _weather.update({'temp': temp, 'cond': cond})
            print(f'weather updated: {cond}  {temp}')
        except Exception as e:
            print(f'weather fetch error: {e}')
        time.sleep(300)   # refresh every 5 minutes


# ── Main loop ─────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description='LM215 clock/weather display')
    ap.add_argument('--host',    default='lm215.local', help='ESP32 hostname or IP')
    ap.add_argument('--port',    type=int, default=80)
    ap.add_argument('--zip', metavar='ZIP', default=os.getenv('ZIP'),
                    help='US zip code (default: ZIP from .env)')
    ap.add_argument('--units',   choices=['imperial', 'metric'], default='imperial',
                    help='Temperature units (default: imperial / °F)')
    args = ap.parse_args()

    if not args.zip:
        ap.error('No zip code found — set OWM_ZIP in client/.env or pass --zip')

    url = f'http://{args.host}:{args.port}/frame'

    threading.Thread(
        target=_weather_thread,
        args=(args.zip, args.units),
        daemon=True,
    ).start()

    print(f'Sending to {url} — Ctrl-C to stop')
    while True:
        now = datetime.datetime.now()
        try:
            buf = blank()
            draw_clock(buf, now)
            with _weather_lock:
                w = dict(_weather)
            draw_panel(buf, now, w)
            post_frame(url, serialise([v ^ 2 for v in buf]))
        except Exception as e:
            print(f'frame error: {e}')

        # sleep to the next whole second
        time.sleep(max(0.0, 1.0 - datetime.datetime.now().microsecond / 1e6))


if __name__ == '__main__':
    main()
