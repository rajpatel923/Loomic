# Loomic Server

Backend server for the Loomic platform. Built with C++20, CMake presets, Ninja, and vcpkg manifest mode.

## Architecture Overview

| Layer | Technology | Port |
|-------|-----------|------|
| TLS TCP binary protocol | Boost.ASIO coroutines | 7777 |
| HTTP REST + WebSocket | Boost.Beast | 8080 |
| Prometheus metrics | prometheus-cpp (CivetWeb) | 9090 |
| Reverse proxy (TLS termination) | nginx | 80 / 443 |
| PostgreSQL | NeonDB (cloud) or local Docker | 5432 |
| Redis | Presence, offline queue, unread counts | 6379 |
| Cassandra | Message history | 10350 |

Real-time messaging supports two protocols:
- **WebSocket** (`ws://host:8080/ws`) — JSON text frames, browser-friendly
- **TLS TCP** (port 7777) — binary wire protocol (LMS2), 30-byte fixed header + protobuf payload

---

## Team Workflow

Use the same commands on every OS:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The canonical build directory is `build/<preset>`. Do not use ad-hoc IDE build trees such as `cmake-build-debug`.

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Git | any | Clone with `--recurse-submodules` |
| CMake | 3.25+ | Presets checked in |
| Ninja | any | Canonical generator |
| C++ compiler | C++20 | GCC 12+, Clang 15+, MSVC 2022 |
| OpenSSL CLI | any | Dev certificate generation |
| Docker | optional | Local databases / nginx stack |

`vcpkg` is included as a git submodule and installs all C++ dependencies automatically during configure.

### Install prerequisites

#### Linux (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y git cmake ninja-build build-essential \
    pkg-config curl zip unzip tar openssl \
    docker.io docker-compose-plugin
```

#### macOS

```bash
brew install git cmake ninja openssl
brew install --cask docker   # optional
```

#### Windows (PowerShell)

```powershell
winget install Git.Git
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install ShiningLight.OpenSSL
winget install Docker.DockerDesktop
```

Restart the shell after installing tools so they are available on `PATH`.

---

## Setup

### 1. Clone the repository

```bash
git clone --recurse-submodules https://github.com/your-org/Loomic.git
cd Loomic/server
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

### 2. Bootstrap vcpkg

Linux / macOS:

```bash
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

Windows:

```powershell
.\vcpkg\bootstrap-vcpkg.bat
```

### 3. Create the environment file

```bash
cp .env.example .env
```

Edit `.env` and fill in values for your environment (see [Environment Variables](#environment-variables) below).

### 4. Generate dev TLS certificates

```bash
bash certs/gen_cert.sh
```

Windows:

```powershell
openssl req -x509 -newkey rsa:4096 -keyout certs\server.key -out certs\server.crt `
    -sha256 -days 365 -nodes `
    -subj "/C=US/ST=Dev/L=Local/O=Loomic/CN=localhost" `
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

### 5. Start databases

```bash
docker run -d --name loomic-pg \
  -e POSTGRES_DB=loomic -e POSTGRES_USER=loomic -e POSTGRES_PASSWORD=secret \
  -p 5432:5432 postgres:16
```

### 6. Apply migrations

```bash
psql "$LOOMIC_PG_CONN_STRING" -f db/migrations/V1__create_auth_tables.sql
```

---

## Build, Test, and Run

### Standard build

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

### Sanitizer builds (Phase 7)

```bash
# AddressSanitizer + UBSan (Clang required)
cmake --preset debug-asan
cmake --build --preset debug-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ctest --preset debug-asan --output-on-failure

# ThreadSanitizer (Clang required)
cmake --preset debug-tsan
cmake --build --preset debug-tsan
TSAN_OPTIONS=halt_on_error=1:suppressions=tsan_suppressions.txt ctest --preset debug-tsan --output-on-failure
```

### Load test tool

Build with `BUILD_TOOLS=ON`:

```bash
cmake --preset debug -DBUILD_TOOLS=ON
cmake --build --preset debug
```

Run against the server:

```bash
./build/debug/bin/load_client \
  --host 127.0.0.1 --port 7777 \
  --threads 4 --sessions-per-thread 250 \
  --rate 10 --duration 30 \
  --jwt <bearer-token>
```

Prints p50 / p99 / p999 latency, throughput, and dropped-message count.
**Target:** p99 < 10 ms, 10 000 concurrent connections.

### Run

```bash
./build/debug/bin/LoomicServer --config config/server.json
```

### Verify

```bash
# REST health
curl http://localhost:8080/health

# Check X-Request-ID tracing header (Phase 7)
curl -v http://localhost:8080/health 2>&1 | grep X-Request-ID

# Prometheus metrics (Phase 7)
curl http://localhost:9090/metrics

# Verify structured JSON log output (Phase 7)
tail -1 logs/server.log | python3 -m json.tool
```

---

## Makefile Shortcuts

```bash
make cert
make build-debug
make build-release
make test
make run-debug
```

---

## Docker

### Full observability stack (Phase 7)

```bash
docker compose up --build
```

This starts:

| Service | Host URL | Purpose |
|---------|----------|---------|
| `server` | http://localhost:8080 | REST API + WebSocket |
| `nginx` | https://localhost:443 | TLS termination + rate limiting |
| `prometheus` | http://localhost:19090 | Metrics storage (scrapes server:9090) |
| `grafana` | http://localhost:3000 | Live dashboard (admin / admin) |

> **TCP binary protocol** (port 7777) connects directly to the server container — nginx does not proxy binary TLS TCP.

### nginx rate limiting

nginx applies `60 requests/minute` per IP (burst 20) on all REST and WebSocket routes. The underlying server has its own per-session TCP rate limit (5 msg/sec, burst 20 frames).

---

## Structured Logging (Phase 7)

All log lines are valid JSON:

```json
{"ts":"2026-04-20T14:32:01.123","tid":12345,"lvl":"info","rid":"550e8400-e29b-41d4-a716-446655440000","msg":"HTTP server: http://0.0.0.0:8080"}
```

| Field | Description |
|-------|-------------|
| `ts` | ISO-8601 timestamp with milliseconds |
| `tid` | OS thread ID |
| `lvl` | `trace` / `debug` / `info` / `warning` / `error` / `critical` |
| `rid` | UUID v4 **X-Request-ID** (empty for background tasks) |
| `msg` | Log message |

To correlate a client request with its log lines, copy the `X-Request-ID` value from any HTTP response header and `grep` for it in `logs/server.log`.

---

## Prometheus Metrics (Phase 7)

The metrics endpoint is at `http://host:9090/metrics`.

| Metric | Type | Description |
|--------|------|-------------|
| `loomic_messages_total` | Counter | Chat messages routed |
| `loomic_http_requests_total{method, status}` | Counter | HTTP requests by method + status |
| `loomic_active_sessions` | Gauge | Authenticated TCP/WS sessions |
| `loomic_active_connections` | Gauge | All open connections (including unauthenticated) |
| `loomic_message_latency_ms` | Histogram | End-to-end message routing latency (buckets: 0.1, 1, 5, 10, 50, 100 ms) |
| `loomic_http_latency_ms` | Histogram | HTTP request latency (buckets: 1, 5, 10, 25, 50, 100, 250 ms) |

The Grafana dashboard (`monitoring/grafana/dashboards/loomic.json`) ships with 4 panels:
- **Message Rate** — `rate(loomic_messages_total[1m])`
- **p99 Message Latency** — `histogram_quantile(0.99, rate(loomic_message_latency_ms_bucket[1m]))`
- **Active Sessions** — `loomic_active_sessions`
- **HTTP Error Rate** — `rate(loomic_http_requests_total{status=~"4..|5.."}[1m])`

---

## CI Pipeline (Phase 7)

Three GitHub Actions jobs run on every push and pull request:

| Job | Runner | Compiler | Flags |
|-----|--------|----------|-------|
| `build-and-test` | ubuntu-24.04 | GCC 14 | standard debug |
| `asan` | ubuntu-24.04 | Clang 17 | `-fsanitize=address,undefined` |
| `tsan` | ubuntu-24.04 | Clang 17 | `-fsanitize=thread` |

vcpkg packages are cached by `vcpkg.json` hash to keep CI fast. The TSan job uses `tsan_suppressions.txt` to silence known false positives in libuv and CivetWeb.

---

## Deployment (GCP)

Pushes to `main` that touch `server/` automatically build a Docker image and deploy it to the GCP VM via GitHub Actions.

### One-time setup

#### 1. Store secrets in GCP Secret Manager

```bash
gcloud secrets create loomic-env-file \
  --data-file=.env --replication-policy=automatic

gcloud secrets create loomic-tls-cert \
  --data-file=certs/server.crt --replication-policy=automatic

gcloud secrets create loomic-tls-key \
  --data-file=certs/server.key --replication-policy=automatic
```

#### 2. Open ports on the GCP VM firewall (Phase 7)

Phase 7 adds nginx (443/80) and Grafana (3000). Open them:

```bash
# HTTPS for nginx (required for production)
gcloud compute firewall-rules create allow-https \
  --allow tcp:443 --target-tags loomic-server

# HTTP redirect (nginx redirects to HTTPS)
gcloud compute firewall-rules create allow-http \
  --allow tcp:80 --target-tags loomic-server

# Grafana — internal access only; consider restricting to your IP
gcloud compute firewall-rules create allow-grafana \
  --allow tcp:3000 --target-tags loomic-server \
  --source-ranges <your-office-ip>/32

# Prometheus — keep private (scraped internally by Grafana)
# Do NOT open port 19090 or 9090 to the public internet
```

> **Important:** Port 9090 (metrics) should **never** be public. It exposes internal server stats. If you need external Prometheus, use a VPN or Cloud Armor.

#### 3. Grant the VM service account access

```bash
PROJECT_NUMBER=$(gcloud projects describe YOUR_PROJECT_ID --format="value(projectNumber)")
CE_SA="${PROJECT_NUMBER}-compute@developer.gserviceaccount.com"

for secret in loomic-env-file loomic-tls-cert loomic-tls-key; do
  gcloud secrets add-iam-policy-binding $secret \
    --member="serviceAccount:${CE_SA}" \
    --role="roles/secretmanager.secretAccessor"
done
```

#### 4. Verify cloud-platform scope on the VM

```bash
gcloud compute instances describe YOUR_VM_NAME \
  --format="value(serviceAccounts[].scopes[])"
```

If `https://www.googleapis.com/auth/cloud-platform` is not listed, stop the VM, edit its access scopes to **Allow full access to all Cloud APIs**, then restart it.

#### 5. Ensure the deploy directory exists on the VM

```bash
mkdir -p /home/g12g23raj_nes/Loomic/server
```

### GitHub Secrets required

| Secret | Description |
|--------|-------------|
| `GCP_PROJECT_ID` | GCP project ID |
| `GCP_REGION` | Artifact Registry region |
| `GAR_REPO` | Artifact Registry repository name |
| `GCP_SA_KEY` | JSON key for the GitHub Actions service account |
| `VM_HOST` | Public IP or hostname of the GCP VM |
| `VM_USER` | SSH username |
| `VM_SSH_KEY` | Private SSH key |

---

## Environment Variables

Copy `.env.example` to `.env` and set the following:

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `LOOMIC_PG_CONN_STRING` | Yes | — | libpqxx connection string for PostgreSQL |
| `LOOMIC_JWT_SECRET` | Yes | — | Secret used to sign and verify JWT tokens |
| `LOOMIC_BIND_ADDRESS` | No | `0.0.0.0` | Interface to bind to |
| `LOOMIC_PORT` | No | `7777` | TLS TCP listener port |
| `LOOMIC_HTTP_HEALTH_PORT` | No | `8080` | HTTP / WebSocket port |
| `LOOMIC_METRICS_PORT` | No | `9090` | Prometheus metrics pull port |
| `LOOMIC_THREAD_COUNT` | No | hardware_concurrency | Worker threads (`0` = auto) |
| `LOOMIC_TLS_CERT_PATH` | Yes | — | Path to TLS certificate |
| `LOOMIC_TLS_KEY_PATH` | Yes | — | Path to TLS private key |
| `LOOMIC_REDIS_HOST` | No | `127.0.0.1` | Redis hostname |
| `LOOMIC_REDIS_PORT` | No | `6379` | Redis port |
| `LOOMIC_CASSANDRA_CONTACT_POINTS` | No | — | Comma-separated Cassandra hosts |
| `LOOMIC_LOG_LEVEL` | No | `info` | `trace` `debug` `info` `warn` `error` |
| `LOOMIC_LOG_FILE` | No | `logs/server.log` | JSON log file path |

---

## CLion

Import the checked-in CMake presets and select the `debug` preset-backed profile. Do not create or use a separate `cmake-build-debug` configuration.
