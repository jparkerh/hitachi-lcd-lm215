# LM215 Client

Python clients for the Hitachi LM215 480×128 LCD over WiFi.

## Setup

```bash
pip install -r requirements.txt
```

### Font (required for clock.py)

The clock uses the **Moonhouse** font by NimaType (freeware).

1. Download from: https://www.fontspace.com/moonhouse-font-f18420
2. Unzip and place the `.ttf` file at:
   ```
   client/fonts/Moonhouse.ttf
   ```

The `fonts/` directory is gitignored — each machine needs its own copy.

### Environment (.env)

Copy `.env.example` to `.env` and fill in your values (already gitignored):

```
ZIP=10001
```

## Usage

```bash
# Live clock + weather (default zip from .env)
python3 clock.py

# Override zip
python3 clock.py --zip 10001

# Metric units
python3 clock.py --units metric

# Alternating bars test
python3 send_bars.py --host lm215.local
```

Weather is provided by [Open-Meteo](https://open-meteo.com/) — free, no API key required.

## Docker (Raspberry Pi)

The container uses host networking so `lm215.local` mDNS resolves correctly, and restarts automatically on reboot.

Make sure `fonts/Moonhouse.ttf` and `.env` are in place on the Pi before starting.

### Option A — build on the Pi

```bash
rsync -av --exclude __pycache__ client/ pi@raspberrypi.local:lm215-client/
ssh pi@raspberrypi.local "cd lm215-client && docker compose up -d --build"
```

This copies everything — including `fonts/` and `.env` — since rsync ignores `.gitignore`.

### Option B — cross-compile on your dev machine and push

```bash
# Build for ARM64 on your dev machine
docker buildx build --platform linux/arm64 -t lm215-clock client/

# Ship it to the Pi along with the compose file, font, and .env
docker save lm215-clock | ssh pi@raspberrypi.local "docker load"
rsync -av --exclude __pycache__ client/ pi@raspberrypi.local:lm215-client/
ssh pi@raspberrypi.local "cd lm215-client && docker compose up -d"
```

### Logs

```bash
ssh pi@raspberrypi.local "cd lm215-client && docker compose logs -f"
```
