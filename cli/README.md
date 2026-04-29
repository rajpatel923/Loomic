# Loomic CLI

A small terminal chat window for the Loomic server. It is a **feature** of the
existing app — it logs in over the public HTTP API, opens a WebSocket to `/ws`,
and lets you chat from the command line. It does not replace or modify the
server.

## Install

```bash
cd cli
python -m venv .venv
. .venv/bin/activate           # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

Python 3.10 or newer.

## Run

```bash
# default: connects to http://127.0.0.1:8080
python loomic.py

# point at a different host/port
python loomic.py --host 35.232.85.186 --port 8080

# production over TLS
python loomic.py --host loomic.example.com --port 443 --http-scheme https --ws-scheme wss
```

On first run you will be prompted for your username and password. The access
token is cached at `~/.loomic/token.json` (mode 0600). To force re-login,
delete that file.

## Commands

| Command              | What it does                                     |
| -------------------- | ------------------------------------------------ |
| `/help`              | list commands                                    |
| `/nick <name>`       | change your displayed name (local only)          |
| `/msg <user> <text>` | private message a user by username               |
| `/log [N]`           | show last N CLI activity entries (default 20)    |
| `/quit`              | disconnect and exit                              |

Anything not starting with `/` is sent to the active conversation (the most
recent one, picked automatically at login).

## Behind the scenes

The `/log` command shows recent CLI activity recorded at
`~/.loomic/activity.log` — connect events, sends, receives, PMs. This is the
"window into what's happening" that the professor asked for.

Server-side activity (connection events, broadcast traffic, errors) lives in
the existing `server/logs/server.log` and is unaffected by this CLI.

## Files

```
cli/
├── loomic.py          # the CLI (single file, ~250 lines)
├── requirements.txt   # `requests`, `websocket-client`
├── .gitignore
└── README.md
```

## Notes

- `/nick` is a **local display alias**. The server's user model has a fixed
  `username`; the CLI just relabels how your own name appears on outgoing
  messages.
- `/msg <user>` looks up the user via `GET /users/search`, computes the DM
  conversation ID per the OpenAPI spec (`min(sender_id, recipient_id)`),
  and sends a `chat` frame to it.
- Ctrl-C closes the WebSocket cleanly via a SIGINT handler.
- The token file is created with mode `0600` so other local users can't read it.
- Token is currently not auto-refreshed — when it expires (24h), delete
  `~/.loomic/token.json` and log in again.
