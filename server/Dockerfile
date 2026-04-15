# Stage 1: builder
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
# Use system ninja/cmake instead of vcpkg downloading x86_64 binaries (required for arm64)
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        make \
        gcc \
        g++ \
        git \
        curl \
        zip \
        unzip \
        tar \
        pkg-config \
        ca-certificates \
        linux-libc-dev \
        bison \
        flex \
        perl \
        python3 \
        autoconf \
        automake \
        libtool \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Clone and bootstrap vcpkg into the source dir so CMakePresets.json toolchain path resolves
RUN git clone https://github.com/microsoft/vcpkg.git vcpkg \
    && ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# Copy dependency manifest first — unchanged vcpkg.json skips re-install
COPY vcpkg.json vcpkg-configuration.json CMakeLists.txt CMakePresets.json ./
COPY src/ src/
COPY include/ include/
COPY proto/ proto/
COPY vcpkg-overlay-ports/ vcpkg-overlay-ports/
COPY tests/ tests/

# cmake configure triggers vcpkg install automatically
RUN cmake --preset release
RUN cmake --build --preset release

# Stage 2: runtime
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/release/bin/LoomicServer /usr/local/bin/LoomicServer
COPY api/ /app/api/
COPY config/ /app/config/

RUN mkdir -p /app/logs

WORKDIR /app

EXPOSE 7777 8080 9090

ENTRYPOINT ["/usr/local/bin/LoomicServer", "--config", "/app/config/server.json"]
