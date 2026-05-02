# Loomic Client (Next.js)

The web frontend for Loomic. A Next.js app that talks to the Loomic backend
over HTTPS + WebSocket and (for live chat) over an SSE bridge backed by the
TLS TCP messaging port.

> **Heads up:** this codebase uses a fork of Next.js with breaking changes
> from upstream. See [AGENTS.md](AGENTS.md) — read the relevant guide in
> `node_modules/next/dist/docs/` before making non-trivial changes.

## Getting Started

```bash
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) in your browser.

## Backend Configuration

The client doesn't need the backend running locally if you point it at a
remote Loomic backend.

1. Create `client/.env.local`.
2. Set `LOOMIC_API_BASE_URL` to the backend you want to use.

```bash
LOOMIC_API_BASE_URL=http://35.232.85.186:8080
```

Restart `npm run dev`. The Next.js route handlers under `src/app/api/*` will
proxy requests to that backend even during local development.

For deployed HTTPS environments the browser WebSocket URL must also be
secure. If your backend supports TLS for `/ws`, set:

```bash
LOOMIC_WS_URL=wss://your-backend-host:8080/ws
```

If `LOOMIC_WS_URL` is not set, the client derives the socket endpoint from
`LOOMIC_API_BASE_URL` and prefers `wss://` when the app itself is served
over HTTPS.

## Live Chat Bridge

The `/chat` page uses a browser-safe bridge:

1. The browser talks to Next.js route handlers under `src/app/api/chat/*`.
2. The Next.js server opens the secure Loomic TCP session on port `7777`.
3. Messages stream back to the browser over Server-Sent Events.

Optional `.env.local` overrides:

```bash
LOOMIC_API_BASE_URL=http://35.232.85.186:8080
LOOMIC_WS_URL=wss://your-backend-host:8080/ws
LOOMIC_TCP_HOST=35.232.85.186
LOOMIC_TCP_PORT=7777
LOOMIC_TCP_SERVERNAME=35.232.85.186
LOOMIC_TCP_VERIFY_TLS=false
```

The frontend keeps recent live messages in session memory because the
backend does not yet expose an HTTP history endpoint that backs this bridge.

## What's in the App

### Pages

| Path | What it does |
|------|--------------|
| `/` | Landing / login experience |
| `/chat` | Direct-message chat UI |

### Backend proxy routes (`src/app/api/`)

The Next.js server exposes thin proxies to the Loomic backend so the browser
never has to hold the JWT directly:

| Proxy route | Backend endpoint |
|-------------|------------------|
| `auth/*` | `/auth/register`, `/auth/login`, `/auth/logout`, `/auth/refresh` |
| `conversations/`, `conversations/[id]/messages`, `conversations/[id]/read` | DM list, history, mark-read |
| `users/[id]`, `users/search` | User profile and search |
| `groups/`, `groups/[id]`, `groups/[id]/members`, `groups/[id]/members/[uid]` | Group create / rename / membership |
| `messages/[msg_id]` | Delete a message |
| `upload`, `files/[uuid]` | File attachment upload + download |
| `push/register` | Web push token registration |
| `chat/socket-config`, `chat/*` | SSE bridge to the TLS TCP backend |

### Feature status

> Authoritative source: [`backend-frontend-gap-report.md`](../backend-frontend-gap-report.md)
> at the repo root.

What the UI currently exposes:

- Account registration and login
- Direct-message conversation list
- Sending and receiving DMs over WebSocket / SSE
- User search to start a new DM
- Sign-out

Recent additions visible in the source tree (per commit `b343dae`):
groups, send-status indicators, profile pictures, and a settings surface —
some are wired through proxy routes and may not yet be reachable from a UI
entry point. See the gap report for the canonical list of missing UI vs
backend features.

## Alternative client

There is also a [Python CLI](../cli/README.md) under `cli/` — it logs in
through the same HTTP API and connects to `/ws`. Useful for quick smoke
tests from a terminal without a browser.

## Stack

- Next.js (forked — see `AGENTS.md`)
- React + TypeScript
- Tailwind / PostCSS
- ESLint
