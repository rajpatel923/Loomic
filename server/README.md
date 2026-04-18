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

### Development shortcut (Windows / PowerShell)

If you just want to start the backend for local development:

```powershell
.\dev.cmd
.\dev.ps1
```

The script bootstraps `vcpkg` if needed, configures the `debug` preset, builds it,
and runs the server with `config/server.json`.

Useful flags:

```powershell
.\dev.ps1 -SkipBuild
.\dev.ps1 -Preset release
.\dev.ps1 -BootstrapVcpkg
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

## Fast Local Dev Loop

Docker is useful for dependency parity, but it is the wrong inner-loop for this repo because every code change forces an image rebuild. The faster workflow is:

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/LoomicServer --config config/server.json
```

Notes:

- The binary already calls `Config::load_dotenv()` on startup, so running it directly from the repo root loads `.env` automatically. You do not need `docker compose` just to get env vars.
- `config/server.json` now uses relative `certs/` and `logs/` paths, so the same file works both locally and in Docker because the working directory is the repo root locally and `/app` in the container.
- If you want databases to stay containerized while the app runs natively, run only the backing services in Docker and keep the server process local.
- After the first configure, incremental rebuilds should only recompile changed files, which is usually seconds instead of a full image rebuild.

Convenience target:

```bash
make run-debug
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

## Deployment (GCP)

Pushes to `main` that touch `server/` automatically build a Docker image and deploy it to the GCP VM via GitHub Actions. The following one-time setup is required before the pipeline works.

### One-time setup

#### 1. Store secrets in GCP Secret Manager

Run these from the `server/` directory with `gcloud` pointed at your project:

```bash
gcloud secrets create loomic-env-file \
  --data-file=.env --replication-policy=automatic

gcloud secrets create loomic-tls-cert \
  --data-file=certs/server.crt --replication-policy=automatic

gcloud secrets create loomic-tls-key \
  --data-file=certs/server.key --replication-policy=automatic
```

To update a secret later (e.g. cert renewal):

```bash
gcloud secrets versions add loomic-tls-cert --data-file=certs/server.crt
gcloud secrets versions add loomic-tls-key  --data-file=certs/server.key
gcloud secrets versions add loomic-env-file --data-file=.env
```

#### 2. Grant the VM service account access

```bash
PROJECT_NUMBER=$(gcloud projects describe YOUR_PROJECT_ID --format="value(projectNumber)")
CE_SA="${PROJECT_NUMBER}-compute@developer.gserviceaccount.com"

for secret in loomic-env-file loomic-tls-cert loomic-tls-key; do
  gcloud secrets add-iam-policy-binding $secret \
    --member="serviceAccount:${CE_SA}" \
    --role="roles/secretmanager.secretAccessor"
done
```

#### 3. Verify the VM has the `cloud-platform` OAuth scope

The VM must have the `cloud-platform` scope to call Secret Manager. Check via:

```bash
gcloud compute instances describe YOUR_VM_NAME \
  --format="value(serviceAccounts[].scopes[])"
```

If `https://www.googleapis.com/auth/cloud-platform` is not listed, stop the VM, edit its access scopes to **Allow full access to all Cloud APIs**, then restart it.

#### 4. Ensure the deploy directory exists on the VM

SSH into the VM and create the working directory if it does not exist:

```bash
mkdir -p /home/g12g23raj_nes/Loomic/server
```

### GitHub Secrets required

Add these under **Settings → Secrets and variables → Actions** in the repository:

| Secret | Description |
|--------|-------------|
| `GCP_PROJECT_ID` | GCP project ID (e.g. `my-project-123`) |
| `GCP_REGION` | Artifact Registry region (e.g. `us-central1`) |
| `GAR_REPO` | Artifact Registry repository name |
| `GCP_SA_KEY` | JSON key for the GitHub Actions service account (used to push Docker images) |
| `VM_HOST` | Public IP or hostname of the GCP VM |
| `VM_USER` | SSH username on the VM |
| `VM_SSH_KEY` | Private SSH key for the VM user |

> `ENV_FILE`, `TLS_CERT`, and `TLS_KEY` are **no longer needed** as GitHub Secrets — the VM fetches them directly from GCP Secret Manager at deploy time.

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
