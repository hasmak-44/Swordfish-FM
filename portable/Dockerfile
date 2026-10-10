FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        autoconf \
        automake \
        autopoint \
        build-essential \
        ca-certificates \
        curl \
        file \
        gettext \
        libfontconfig1-dev \
        libfreetype6-dev \
        libfox-1.6-dev \
        libnotify-dev \
        libopengl0 \
        libpng-dev \
        libpolkit-agent-1-dev \
        libstartup-notification0-dev \
        libudisks2-dev \
        libtool \
        libx11-dev \
        libx11-xcb-dev \
        libxcb-util-dev \
        libxcb1-dev \
        libxft-dev \
        libxrandr-dev \
        libxrender-dev \
        pkg-config \
        librsvg2-bin \
        python3 \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

COPY build-portable-in-container /usr/local/bin/build-portable-in-container
RUN chmod 755 /usr/local/bin/build-portable-in-container
