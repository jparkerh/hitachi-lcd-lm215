FROM python:3.12-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
        libfreetype6 \
        libjpeg62-turbo \
        zlib1g \
        libnss-mdns \
        fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/* \
    && sed -i 's/^\(hosts:.*\)dns/\1mdns4_minimal [NOTFOUND=return] dns/' /etc/nsswitch.conf

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY clock.py send_bars.py ./

# Font must be present in client/fonts/ before building the image.
# Download Moonhouse from https://www.fontspace.com/moonhouse-font-f18420
# and place Moonhouse.ttf there first.
COPY fonts/Moonhouse.ttf fonts/Moonhouse.ttf

CMD ["python3", "-u", "clock.py"]
