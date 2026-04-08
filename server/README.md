# LoomicServer

Backend server for the Loomic platform. Built in C++20 using Boost.Beast for HTTP and TLS TCP transport.

**Port layout:**

| Port | Protocol | Purpose |
|------|----------|---------|
| 8080 | HTTP | Health check (`/health`) |
| 7777 | TLS TCP | Main application traffic |
| 9090 | HTTP | Prometheus metrics |

---

## Prerequisites

Both Linux and Windows require the following tools:

| Tool | Version | Notes |
|------|---------|-------|
| Git | any | Required to clone with submodules (vcpkg) |
| CMake | ≥ 3.25 | [cmake.org](https://cmake.org/download/) |
| Ninja | any | Build system used by CMake presets |
| C++ compiler | C++20 support | GCC ≥ 12 / Clang ≥ 15 / MSVC 2022 |
| OpenSSL CLI | any | Used to generate dev TLS certificates |
| Docker + Docker Compose | any | Optional — for local PostgreSQL |

> **vcpkg** is included as a Git submodule and manages all C++ library dependencies automatically. No separate install step is needed beyond bootstrapping it once.

---

## Setup

<details>
<summary><strong>Linux (Ubuntu / Debian)</strong></summary>

### 1. Install system packages

```bash
sudo apt update
sudo apt install -y git cmake ninja-build build-essential \
    pkg-config curl zip unzip tar openssl \
    docker.io docker-compose-plugin
```

For a newer Clang (optional but recommended):

```bash
sudo apt install -y clang-15 libc++-15-dev libc++abi-15-dev
```

### 2. Clone the repository

```bash
git clone --recurse-submodules https://github.com/your-org/Loomic.git
cd Loomic/server
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### 3. Bootstrap vcpkg (first time only)

```bash
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

### 4. Configure environment

```bash
cp .env.example .env
# Edit .env and fill in your values (see Environment Variables section below)
nano .env
```

### 5. Generate TLS certificates (dev)

```bash
make cert
# or: bash certs/gen_cert.sh
```

This creates `certs/server.crt` and `certs/server.key` (self-signed, valid for localhost).

### 6. Start PostgreSQL

**Option A — Docker (recommended for local dev):**

```bash
# Add a postgres service to docker-compose.yml if needed, or use:
docker run -d \
  --name loomic-pg \
  -e POSTGRES_DB=loomic \
  -e POSTGRES_USER=loomic \
  -e POSTGRES_PASSWORD=secret \
  -p 5432:5432 \
  postgres:16
```

Then set in `.env`:
```
LOOMIC_PG_CONN_STRING=host=127.0.0.1 port=5432 dbname=loomic user=loomic password=secret sslmode=disable
```

**Option B — NeonDB (cloud):** Paste your connection string directly into `LOOMIC_PG_CONN_STRING` in `.env`.

### 7. Apply database migrations

```bash
psql "$LOOMIC_PG_CONN_STRING" -f db/migrations/V1__create_auth_tables.sql
```

### 8. Build

```bash
make build-debug
```

Or use CMake directly:

```bash
cmake --preset debug
cmake --build --preset debug
```

### 9. Run

```bash
./build/debug/bin/LoomicServer --config config/server.json
```

### 10. Verify

```bash
curl http://localhost:8080/health
# → {"status":"ok"}
```

</details>

<details>
<summary><strong>Windows (PowerShell + winget)</strong></summary>

### 1. Install prerequisites

Open PowerShell as Administrator:

```powershell
winget install Git.Git
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Install OpenSSL (choose one):

```powershell
# Option A — winget
winget install ShiningLight.OpenSSL

# Option B — chocolatey
choco install openssl
```

Install Docker Desktop for PostgreSQL:

```powershell
winget install Docker.DockerDesktop
```

Restart your shell after installation so all tools are on `PATH`.

### 2. Clone the repository

```powershell
git clone --recurse-submodules https://github.com/your-org/Loomic.git
cd Loomic\server
```

### 3. Bootstrap vcpkg (first time only)

```powershell
.\vcpkg\bootstrap-vcpkg.bat
```

### 4. Configure environment

```powershell
Copy-Item .env.example .env
# Open .env in your editor and fill in values
notepad .env
```

### 5. Generate TLS certificates (dev)

```powershell
openssl req -x509 -newkey rsa:4096 -keyout certs\server.key -out certs\server.crt `
    -sha256 -days 365 -nodes `
    -subj "/C=US/ST=Dev/L=Local/O=Loomic/CN=localhost" `
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

### 6. Start PostgreSQL

Open Docker Desktop, then:

```powershell
docker run -d `
  --name loomic-pg `
  -e POSTGRES_DB=loomic `
  -e POSTGRES_USER=loomic `
  -e POSTGRES_PASSWORD=secret `
  -p 5432:5432 `
  postgres:16
```

Set in `.env`:
```
LOOMIC_PG_CONN_STRING=host=127.0.0.1 port=5432 dbname=loomic user=loomic password=secret sslmode=disable
```

### 7. Apply database migrations

```powershell
$env:PGPASSWORD = "secret"
psql -h 127.0.0.1 -U loomic -d loomic -f db\migrations\V1__create_auth_tables.sql
```

### 8. Build

```powershell
cmake --preset debug
cmake --build --preset debug
```

### 9. Run

```powershell
.\build\debug\bin\LoomicServer.exe --config config\server.json
```

### 10. Verify

```powershell
Invoke-RestMethod http://localhost:8080/health
# or: curl http://localhost:8080/health
```

</details>

<details>
<summary><strong>macOS</strong></summary>

```bash
brew install cmake ninja openssl

git clone --recurse-submodules https://github.com/your-org/Loomic.git
cd Loomic/server

./vcpkg/bootstrap-vcpkg.sh -disableMetrics

cp .env.example .env
# Edit .env

make cert
make build-debug

./build/debug/bin/LoomicServer --config config/server.json
curl http://localhost:8080/health
```

</details>

---

## Docker (any platform)

If you just want to run the server without a native build:

```bash
docker compose up --build
```

The `docker-compose.yml` starts the server container with all three ports exposed (`7777`, `8080`, `9090`), mounts `./config`, `./logs`, and `./certs` from the host, and reads secrets from `.env`. You still need to generate certs and run migrations before the first start.

---

## Environment Variables

Copy `.env.example` to `.env` and set the following:

| Variable | Required | Description |
|----------|----------|-------------|
| `LOOMIC_PG_CONN_STRING` | Yes | libpqxx connection string for PostgreSQL |
| `LOOMIC_JWT_SECRET` | Yes | Secret used to sign / verify JWT tokens |
| `LOOMIC_BIND_ADDRESS` | No | Interface to bind to (default: `0.0.0.0`) |
| `LOOMIC_PORT` | No | TLS TCP listener port (default: `7777`) |
| `LOOMIC_HTTP_HEALTH_PORT` | No | HTTP health check port (default: `8080`) |
| `LOOMIC_THREAD_COUNT` | No | Worker threads; `0` = use hardware concurrency |
| `LOOMIC_TLS_CERT_PATH` | Yes | Path to TLS certificate file |
| `LOOMIC_TLS_KEY_PATH` | Yes | Path to TLS private key file |
| `LOOMIC_REDIS_HOST` | No | Redis hostname (default: `127.0.0.1`) |
| `LOOMIC_REDIS_PORT` | No | Redis port (default: `6379`) |
| `LOOMIC_SCYLLA_HOSTS` | No | Comma-separated ScyllaDB hosts |
| `LOOMIC_LOG_LEVEL` | No | Log verbosity: `trace`, `debug`, `info`, `warn`, `error` |
| `LOOMIC_LOG_FILE` | No | Log file path (default: `logs/server.log`) |
| `LOOMIC_METRICS_PORT` | No | Prometheus metrics port (default: `9090`) |

---

## Database Schema

Migrations live in `db/migrations/`. Apply them in order against your PostgreSQL database:

```bash
# Linux / macOS
psql "$LOOMIC_PG_CONN_STRING" -f db/migrations/V1__create_auth_tables.sql

# Windows PowerShell
psql -h <host> -U <user> -d <dbname> -f db\migrations\V1__create_auth_tables.sql
```

---

## Tests

```bash
make test
# or manually:
cd build/debug && ctest --output-on-failure
```

---

## Makefile Quick Reference

| Target | Description |
|--------|-------------|
| `make cert` | Generate a self-signed dev TLS certificate in `certs/` |
| `make build-debug` | Configure and build a debug binary |
| `make build-release` | Configure and build a release binary |
| `make test` | Build (debug) then run all tests via ctest |
| `make clean` | Remove the `build/` directory |

---

## C++ Dependencies

Managed via [vcpkg](https://vcpkg.io) (`vcpkg.json`):

- **Networking:** boost-asio, boost-beast, boost-system
- **Logging:** spdlog
- **JSON:** nlohmann-json
- **TLS:** openssl
- **Cache:** hiredis
- **Auth:** jwt-cpp
- **Metrics:** prometheus-cpp
- **Database:** libpqxx
- **Utilities:** abseil
- **Testing:** gtest
