FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive

# pipewire, wireplumber, pipewire-pulse, pulseaudio-utils and libspa-0.2-dev
# are only used by the container test pw_integration (T3), which runs a
# private PipeWire session against a fake Fireface 802; nothing is started
# automatically. The versions come from resolute(-updates), as on the
# Kubuntu 26.04 host (validated: PipeWire 1.6.2-1ubuntu1.2, WirePlumber
# 0.5.13-1ubuntu1.1); the test checks the 1.6.x / 0.5.x series.

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       cmake \
       pkg-config \
       libusb-1.0-0-dev \
       libasound2-dev \
       alsa-ucm-conf \
       pipewire \
       pipewire-bin \
       pipewire-pulse \
       wireplumber \
       pulseaudio-utils \
       libspa-0.2-dev \
       git \
       ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --uid 1001 --shell /bin/bash developer

USER developer
