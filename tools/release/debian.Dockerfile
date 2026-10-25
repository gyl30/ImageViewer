FROM debian:12-slim@sha256:7ff6ba31527b9e336162fa061746c366c4b2032f4d2cff2078bea43f78efe3f8
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build dpkg-dev file desktop-file-utils \
    qt6-base-dev libqt6svg6-dev qt6-image-formats-plugins \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
