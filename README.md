# Loomic

A real-time chat platform with a C++ backend, a Next.js web client, and a
Python CLI client.

## Repository tour

| Folder | What's inside |
|--------|---------------|
| [`server/`](server/README.md) | C++20 backend — Boost.ASIO TCP, Boost.Beast HTTP/WS, Postgres, Redis, Cassandra, Prometheus metrics. Built with CMake + vcpkg. |
| [`client/`](client/README.md) | Next.js web frontend — login, DMs, group proxy routes, file upload proxy. Talks to the backend over HTTPS + WebSocket + an SSE bridge to TLS TCP. |
| [`cli/`](cli/README.md) | Python CLI chat client — connects over the same WebSocket the web frontend uses. Single-file `loomic.py`. |
| [`python_test/`](python_test/README.md) | End-to-end test script that exercises HTTP auth + TLS TCP messaging. |
| `backend-frontend-gap-report.md` | Living audit of which backend features are exposed in the frontend yet. |

The `server/vcpkg/` directory is a git submodule (Microsoft's C++ package
manager) — clone with `--recurse-submodules` or run `git submodule update
--init --recursive`.

## Default ports

| Port | Purpose |
|------|---------|
| `7777` | TLS TCP binary protocol (LMS2) |
| `8080` | HTTP REST + WebSocket (`/ws`) |
| `9090` | Prometheus metrics |
| `3000` | Next.js dev server (web client) |
| `3001` | Grafana dashboard (host port → container 3000) |

## Quick start

```bash
git clone --recurse-submodules https://github.com/rajpatel923/Loomic.git
cd Loomic
```

Then follow the README inside whichever component you're working on:

- **Backend:** [server/README.md](server/README.md)
- **Web client:** [client/README.md](client/README.md)
- **CLI client:** [cli/README.md](cli/README.md)
- **E2E tests:** [python_test/README.md](python_test/README.md)

## Branch layout

| Branch | Purpose |
|--------|---------|
| `main` | Production. Pushes here trigger the GCP deploy job for `server/` changes. |
| `command-line-interface` | Active feature branch for the Python CLI client. |
| `feature/Maaz-Frontend` | Frontend feature branch. |
| `server_p5` | Server work-in-progress. |

See `.github/workflows/` for the CI/CD pipeline.
