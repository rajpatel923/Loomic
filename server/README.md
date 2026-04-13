# LoomicServer

Backend server for the Loomic platform. The project is built with C++20, CMake presets, Ninja, and vcpkg manifest mode.

## Team Workflow

Use the same commands on every OS:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The canonical build directory is `build/<preset>`. Do not use ad-hoc IDE build trees such as `cmake-build-debug`.

## Prerequisites

Required on Windows, Linux, and macOS:

| Tool | Version | Notes |
|------|---------|-------|
| Git | any | Clone with submodules |
| CMake | 3.25+ | Presets are checked in |
| Ninja | any | Canonical generator |
| C++ compiler | C++20 support | GCC 12+, Clang 15+, or MSVC 2022 |
| OpenSSL CLI | any | Dev certificate generation |
| Docker Desktop / Docker Engine | optional | Local PostgreSQL |

`vcpkg` is included as a git submodule and installs C++ dependencies automatically during configure.

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
```

If you want local PostgreSQL in Docker:

```bash
brew install --cask docker
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

Linux / macOS:

```bash
cp .env.example .env
```

Windows:

```powershell
Copy-Item .env.example .env
```

Edit `.env` and set the values for your environment.

### 4. Generate dev TLS certificates

Linux / macOS:

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

### 5. Start PostgreSQL

Example local Docker database:

```bash
docker run -d \
  --name loomic-pg \
  -e POSTGRES_DB=loomic \
  -e POSTGRES_USER=loomic \
  -e POSTGRES_PASSWORD=secret \
  -p 5432:5432 \
  postgres:16
```

Set `LOOMIC_PG_CONN_STRING` in `.env`, for example:

```dotenv
LOOMIC_PG_CONN_STRING=host=127.0.0.1 port=5432 dbname=loomic user=loomic password=secret sslmode=disable
```

For Neon production usage, prefer the pooled endpoint (`-pooler`) and include
libpq keepalive settings so stale sockets are detected and replaced promptly:

```dotenv
LOOMIC_PG_CONN_STRING=host=<project>-pooler.<region>.aws.neon.tech port=5432 dbname=<dbname> user=<user> password=<password> sslmode=require connect_timeout=5 keepalives=1 keepalives_idle=30 keepalives_interval=10 keepalives_count=3
```

### 6. Apply migrations

Linux / macOS:

```bash
psql "$LOOMIC_PG_CONN_STRING" -f db/migrations/V1__create_auth_tables.sql
```

Windows:

```powershell
$env:PGPASSWORD = "secret"
psql -h 127.0.0.1 -U loomic -d loomic -f db\migrations\V1__create_auth_tables.sql
```

## Build, Test, and Run

### Configure

```bash
cmake --preset debug
```

### Build

```bash
cmake --build --preset debug
```

### Test

```bash
ctest --preset debug
```

### Run

Linux / macOS:

```bash
./build/debug/bin/LoomicServer --config config/server.json
```

Windows:

```powershell
.\build\debug\bin\LoomicServer.exe --config config\server.json
```

### Verify

Linux / macOS:

```bash
curl http://localhost:8080/health
```

Windows:

```powershell
Invoke-RestMethod http://localhost:8080/health
```

## Makefile Shortcuts

The Makefile is a convenience wrapper for POSIX shells only:

```bash
make cert
make build-debug
make build-release
make test
```

These targets call the same preset-based workflow documented above.

## Docker

To run with Docker Compose:

```bash
docker compose up --build
```

The compose setup uses `.env`, mounts `config/`, `logs/`, and `certs/`, and exposes the server ports.

## Environment Variables

Copy `.env.example` to `.env` and set the following:

| Variable | Required | Description |
|----------|----------|-------------|
| `LOOMIC_PG_CONN_STRING` | Yes | libpqxx connection string for PostgreSQL |
| `LOOMIC_JWT_SECRET` | Yes | Secret used to sign and verify JWT tokens |
| `LOOMIC_BIND_ADDRESS` | No | Interface to bind to |
| `LOOMIC_PORT` | No | TLS TCP listener port |
| `LOOMIC_HTTP_HEALTH_PORT` | No | HTTP health endpoint port |
| `LOOMIC_THREAD_COUNT` | No | Worker threads, `0` uses hardware concurrency |
| `LOOMIC_TLS_CERT_PATH` | Yes | Path to TLS certificate |
| `LOOMIC_TLS_KEY_PATH` | Yes | Path to TLS private key |
| `LOOMIC_REDIS_HOST` | No | Redis hostname |
| `LOOMIC_REDIS_PORT` | No | Redis port |
| `LOOMIC_SCYLLA_HOSTS` | No | Comma-separated ScyllaDB hosts |
| `LOOMIC_LOG_LEVEL` | No | `trace`, `debug`, `info`, `warn`, `error` |
| `LOOMIC_LOG_FILE` | No | Log file path |
| `LOOMIC_METRICS_PORT` | No | Prometheus metrics port |

## CLion

If you use CLion, import the checked-in CMake presets and select the `debug` preset-backed profile. Do not create or use a separate `cmake-build-debug` configuration for this repo.
