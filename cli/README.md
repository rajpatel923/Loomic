# Loomic CLI

A small terminal chat window for the Loomic server. It is a **feature** of the
existing app — it logs in over the public HTTP API, opens a WebSocket to `/ws`,
and lets you chat from the command line. It does not replace or modify the
server.

## Install

```bash
cd cli
pip install -r requirements.txt
```

Python 3.10 or newer.

## Run

```bash
# local dev server
python loomic.py --port 8080

# hosted server (no --port needed, defaults to 443)
python loomic.py --host loomic-server.patel-raj.com --http-scheme https --ws-scheme wss
```

On first run you will be prompted for your username and password. The access
token is cached at `~/.loomic/token.json` (mode 0600). To force re-login,
delete that file.

## Commands

| Command                  | What it does                                        |
| ------------------------ | --------------------------------------------------- |
| `/help`                  | list commands                                       |
| `/nick <name>`           | change your displayed name (local only)             |
| `/msg <user> <text>`     | send a private message to a user by username        |
| `/history <user> [N]`    | show last N messages with a user (default 20)       |
| `/log [N]`               | show last N CLI activity entries (default 20)       |
| `/quit`                  | disconnect and exit                                 |

Anything not starting with `/` is sent to the active conversation (set
automatically at login, or updated after `/msg` and `/history`).

## Behind the scenes

**Messaging flow:**
- `/msg` searches for the user via `GET /users/search`, looks up or creates the
  DM conversation via `POST /conversations`, then sends a `chat` frame over the
  WebSocket.
- `/history` fetches past messages from `GET /conversations/{id}/messages` and
  decodes the base64 content returned by the server.
- A background ping is sent every 5 seconds to keep the WebSocket connection
  alive.

**Offline messages:** Messages sent while you are offline are queued on the
server (up to 7 days) and delivered automatically when you reconnect.

**Activity log:** The `/log` command shows recent CLI activity recorded at
`~/.loomic/activity.log` — connect events, sends, receives, and PMs. This is
the "window into what's happening" that the professor asked for.

Server-side activity lives in `server/logs/server.log` and is unaffected by
this CLI.

## Files

```
cli/
├── loomic.py          # the CLI (single file)
├── requirements.txt   # `requests`, `websocket-client`
├── .gitignore
└── README.md
```

## Notes

- `/nick` is a **local display alias**. The server's user model has a fixed
  `username`; the CLI just relabels how your own name appears on outgoing
  messages.
- Ctrl-C closes the WebSocket cleanly via a SIGINT handler.
- The token file is created with mode `0600` so other local users can't read it.
- Token is not auto-refreshed — when it expires (24h), delete
  `~/.loomic/token.json` and log in again.
