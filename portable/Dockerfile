ARG BASE_IMAGE=debian:buster-slim
FROM ${BASE_IMAGE}
ARG BASE_IMAGE


ENV DEBIAN_FRONTEND=noninteractive

# End-of-life Debian releases are only available from archive.debian.org.
RUN case "$BASE_IMAGE" in \
        *buster*|*stretch*) \
            codename=${BASE_IMAGE#*:}; codename=${codename%-slim}; \
            printf 'deb http://archive.debian.org/debian %s main\n' "$codename" > /etc/apt/sources.list; \
            printf 'deb http://archive.debian.org/debian-security %s/updates main\n' "$codename" >> /etc/apt/sources.list; \
            printf 'Acquire::Check-Valid-Until "false";\n' > /etc/apt/apt.conf.d/99archive ;; \
    esac

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
        $(apt-cache show libxcb-util-dev >/dev/null 2>&1 && echo libxcb-util-dev || echo libxcb-util0-dev) \
        libxcb1-dev \
        libxft-dev \
        libxrandr-dev \
        libxrender-dev \
        pkg-config \
        librsvg2-bin \
        python3 \
        unzip \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

COPY build-portable-in-container /usr/local/bin/build-portable-in-container
RUN chmod 755 /usr/local/bin/build-portable-in-container
