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
