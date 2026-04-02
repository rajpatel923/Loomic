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
        git \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install Conan 2
RUN pip3 install --break-system-packages "conan==2.*"

WORKDIR /app

# Copy only dependency manifest first — Docker caches this layer until conanfile.py changes
COPY conanfile.py .

# Detect Conan profile and install dependencies (cached unless conanfile.py changes)
RUN conan profile detect --force
RUN conan install . \
        --output-folder=build/release \
        --build=missing \
        -s build_type=Release \
        -o "postgresql/*:with_openssl=True"

# Copy the rest of the source (changes here won't re-trigger the Conan layer above)
COPY . .

# Configure and build
RUN cmake --preset release
RUN cmake --build --preset release

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        libssl3 \
        libpq5 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/release/bin/LoomicServer /usr/local/bin/LoomicServer
COPY --from=builder /app/config/ /app/config/

RUN mkdir -p /app/logs

WORKDIR /app

EXPOSE 7777 8080 9090

ENTRYPOINT ["/usr/local/bin/LoomicServer", "--config", "/app/config/server.json"]
