# ── Stage 1: builder ──────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        make \
        ninja-build \
        gcc \
        g++ \
        python3-pip \
        libssl-dev \
        libpq-dev \
        git \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install Conan 2
RUN pip3 install --break-system-packages "conan==2.*"

WORKDIR /app

# Copy source
COPY . .

# Detect Conan profile and install dependencies
RUN conan profile detect --force
RUN conan install . \
        --output-folder=build/release \
        --build=missing \
        -s build_type=Release

# Configure and build
RUN cmake --preset release
RUN cmake --build --preset release

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libssl3 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/release/bin/LoomicServer /usr/local/bin/LoomicServer
COPY --from=builder /app/config/ /app/config/

RUN mkdir -p /app/logs

WORKDIR /app

EXPOSE 7777 8080 9090

ENTRYPOINT ["/usr/local/bin/LoomicServer", "--config", "/app/config/server.json"]
