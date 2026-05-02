"""Loomic CLI — a small terminal chat window for the existing Loomic server.

Logs in over HTTP, then opens a WebSocket to /ws and lets the user chat.
Slash commands: /help /nick /msg /log /quit.
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
import signal
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import requests
import websocket  # from the `websocket-client` package


CONFIG_DIR  = Path.home() / ".loomic"
TOKEN_FILE  = CONFIG_DIR / "token.json"
ACTIVITY_LOG = CONFIG_DIR / "activity.log"


def stamp() -> str:
    return datetime.now().strftime("%H:%M:%S")


def info(msg: str) -> None:
    print(f"[{stamp()}] *** {msg}")


def err(msg: str) -> None:
    print(f"[{stamp()}] !!! {msg}", file=sys.stderr)


def save_token(data: dict) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    TOKEN_FILE.write_text(json.dumps(data))
    try:
        os.chmod(TOKEN_FILE, 0o600)
    except OSError:
        pass


def load_token() -> dict | None:
    if not TOKEN_FILE.exists():
        return None
    try:
        return json.loads(TOKEN_FILE.read_text())
    except (OSError, ValueError):
        return None


def append_activity(line: str) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    with ACTIVITY_LOG.open("a", encoding="utf-8") as f:
        f.write(f"[{datetime.now().isoformat(timespec='seconds')}] {line}\n")


def http_register(base_url: str, username: str, email: str, password: str) -> None:
    r = requests.post(
        f"{base_url}/auth/register",
        json={"username": username, "email": email, "password": password},
        timeout=10,
    )
    if r.status_code not in (200, 201):
        raise RuntimeError(f"registration failed ({r.status_code}): {r.text.strip()[:200]}")


def http_login(base_url: str, username: str, password: str) -> dict:
    r = requests.post(
        f"{base_url}/auth/login",
        json={"username": username, "password": password},
        timeout=10,
    )
    if r.status_code != 200:
        raise RuntimeError(f"login failed ({r.status_code}): {r.text.strip()[:200]}")
    body = r.json()
    body["username"] = username
    return body


def http_search_user(base_url: str, token: str, query: str) -> list[dict]:
    r = requests.get(
        f"{base_url}/users/search",
        params={"q": query, "limit": 5},
        headers={"Authorization": f"Bearer {token}"},
        timeout=10,
    )
    if r.status_code != 200:
        raise RuntimeError(f"user search failed ({r.status_code}): {r.text.strip()[:200]}")
    return r.json().get("users", [])


def http_list_conversations(base_url: str, token: str) -> list[dict]:
    r = requests.get(
        f"{base_url}/conversations",
        headers={"Authorization": f"Bearer {token}"},
        timeout=10,
    )
    if r.status_code != 200:
        raise RuntimeError(f"list convos failed ({r.status_code}): {r.text.strip()[:200]}")
    body = r.json()
    return body if isinstance(body, list) else body.get("value", [])


def http_get_messages(base_url: str, token: str, conv_id: str, limit: int = 20) -> list[dict]:
    r = requests.get(
        f"{base_url}/conversations/{conv_id}/messages",
        params={"limit": limit},
        headers={"Authorization": f"Bearer {token}"},
        timeout=10,
    )
    if r.status_code != 200:
        raise RuntimeError(f"fetch messages failed ({r.status_code}): {r.text.strip()[:200]}")
    body = r.json()
    return body if isinstance(body, list) else body.get("messages", [])


def http_get_or_create_dm(base_url: str, token: str, peer_id: str) -> str:
    convos = http_list_conversations(base_url, token)
    for c in convos:
        if c.get("kind") == "dm" and str(c.get("peer_id")) == str(peer_id):
            return str(c["id"])
    r = requests.post(
        f"{base_url}/conversations",
        json={"kind": "dm", "member_ids": [peer_id]},
        headers={"Authorization": f"Bearer {token}"},
        timeout=10,
    )
    if r.status_code not in (200, 201):
        raise RuntimeError(f"create DM failed ({r.status_code}): {r.text.strip()[:200]}")
    return str(r.json()["conv_id"])


class ChatSession:
    """Holds runtime state and runs the receive loop."""

    def __init__(self, base_url: str, ws_url: str, auth: dict):
        self.base_url = base_url
        self.ws_url   = ws_url
        self.token    = auth["access_token"]
        self.username = auth["username"]
        self.nick     = auth["username"]   # local display name; /nick changes this
        self.active_conv: str | None = None
        self.peers_by_id: dict[str, str] = {}   # user_id -> username (best-effort)
        self.ws: websocket.WebSocket | None = None
        self.stop = threading.Event()

    # ---- WebSocket -------------------------------------------------------

    def connect(self) -> None:
        self.ws = websocket.WebSocket()
        self.ws.connect(self.ws_url, timeout=10)
        self.ws.send(json.dumps({"type": "auth", "token": self.token}))
        info(f"connected to {self.ws_url} as {self.nick}")
        append_activity(f"connect ws as {self.username}")
        t = threading.Thread(target=self._ping_loop, daemon=True)
        t.start()

    def _ping_loop(self) -> None:
        while not self.stop.is_set():
            time.sleep(5)
            if self.stop.is_set():
                break
            try:
                self.ws.send(json.dumps({"type": "ping"}))
            except Exception:
                break

    def close(self) -> None:
        self.stop.set()
        if self.ws is not None:
            try: self.ws.close()
            except Exception: pass

    def send_chat(self, text: str) -> None:
        if not self.active_conv:
            err("no active conversation. use /msg <user> <text> to PM, or pick one with /list.")
            return
        self.ws.send(json.dumps({
            "type": "chat",
            "conv_id": self.active_conv,
            "content": text,
        }))
        append_activity(f"send conv={self.active_conv} text={text!r}")

    # ---- receive loop ----------------------------------------------------

    def recv_loop(self) -> None:
        while not self.stop.is_set():
            try:
                raw = self.ws.recv()
            except Exception as e:
                if not self.stop.is_set():
                    err(f"connection lost: {e}")
                    self.stop.set()
                return
            if not raw:
                continue
            try:
                msg = json.loads(raw)
            except ValueError:
                continue
            self.render(msg)

    def render(self, msg: dict) -> None:
        t = msg.get("type")
        if t == "chat":
            sender = self.peers_by_id.get(msg.get("sender_id", ""), msg.get("sender_id", "?"))
            ts = self._fmt_ts(msg.get("ts_ms"))
            print(f"\r[{ts}] <{sender}> {msg.get('content', '')}")
            print("> ", end="", flush=True)
            append_activity(f"recv chat from={sender} content={msg.get('content','')!r}")
        elif t == "delivered":
            append_activity(f"recv delivered msg_id={msg.get('msg_id')}")
        elif t == "read":
            append_activity(f"recv read conv={msg.get('conv_id')} user={msg.get('user_id')}")
        elif t == "pong":
            pass
        elif t == "error":
            err(f"server error: {msg}")
        else:
            print(f"\r[{stamp()}] *** {msg}")
            print("> ", end="", flush=True)

    @staticmethod
    def _fmt_ts(ts_ms) -> str:
        try:
            return datetime.fromtimestamp(int(ts_ms) / 1000).strftime("%H:%M:%S")
        except (TypeError, ValueError):
            return stamp()


# ---- slash commands -----------------------------------------------------

HELP_TEXT = """\
commands:
  /help                   show this list
  /nick <name>            change your displayed name (local only)
  /msg <user> <text>      private message a user by username
  /history <user> [N]     show last N messages with a user (default 20)
  /log [N]                show last N lines of CLI activity (default 20)
  /quit                   disconnect and exit
anything else             send to the active conversation
"""


def cmd_help(_: ChatSession, _args: str) -> None:
    print(HELP_TEXT, end="")


def cmd_nick(s: ChatSession, args: str) -> None:
    new = args.strip()
    if not new:
        err("usage: /nick <name>")
        return
    old, s.nick = s.nick, new
    info(f"now displaying as '{new}' (was '{old}')")


def cmd_msg(s: ChatSession, args: str) -> None:
    parts = args.split(maxsplit=1)
    if len(parts) < 2:
        err("usage: /msg <user> <text>")
        return
    target_username, text = parts
    try:
        users = http_search_user(s.base_url, s.token, target_username)
    except Exception as e:
        err(str(e))
        return
    match = next((u for u in users if u.get("username", "").lower() == target_username.lower()), None)
    if not match:
        err(f"no such user: {target_username}")
        return
    target_id = str(match.get("id"))
    s.peers_by_id[target_id] = match["username"]
    try:
        conv_id = http_get_or_create_dm(s.base_url, s.token, target_id)
    except Exception as e:
        err(str(e))
        return
    s.ws.send(json.dumps({"type": "chat", "conv_id": conv_id, "content": text}))
    info(f"sent PM to {match['username']}")
    append_activity(f"pm to={match['username']} text={text!r}")
    if s.active_conv is None:
        s.active_conv = conv_id
        info(f"active conversation set to {match['username']} ({conv_id})")


def cmd_log(_: ChatSession, args: str) -> None:
    n = 20
    if args.strip():
        try: n = max(1, int(args.strip()))
        except ValueError: pass
    if not ACTIVITY_LOG.exists():
        info("no activity logged yet")
        return
    lines = ACTIVITY_LOG.read_text(encoding="utf-8", errors="replace").splitlines()[-n:]
    print(f"--- last {len(lines)} activity entries ---")
    for line in lines:
        print(line)
    print("--- end ---")


def cmd_history(s: ChatSession, args: str) -> None:
    parts = args.split()
    if not parts:
        err("usage: /history <user> [N]")
        return
    target_username = parts[0]
    limit = 20
    if len(parts) > 1:
        try: limit = max(1, int(parts[1]))
        except ValueError: pass
    try:
        users = http_search_user(s.base_url, s.token, target_username)
    except Exception as e:
        err(str(e)); return
    match = next((u for u in users if u.get("username", "").lower() == target_username.lower()), None)
    if not match:
        err(f"no such user: {target_username}"); return
    target_id = str(match.get("id"))
    try:
        conv_id = http_get_or_create_dm(s.base_url, s.token, target_id)
        messages = http_get_messages(s.base_url, s.token, conv_id, limit)
    except Exception as e:
        err(str(e)); return
    my_id = _decode_jwt_subject(s.token)
    print(f"--- conversation with {match['username']} ---")
    for m in reversed(messages):
        sender_id = str(m.get("sender_id", ""))
        sender = "you" if sender_id == my_id else match["username"]
        ts = ChatSession._fmt_ts(m.get("ts_ms") or m.get("ts"))
        import base64
        raw = m.get("content_b64") or m.get("content", "")
        try:
            content = base64.b64decode(raw).decode("utf-8", errors="replace") if raw else ""
        except Exception:
            content = str(raw)
        print(f"[{ts}] <{sender}> {content}")
    print("--- end ---")
    s.active_conv = conv_id


def cmd_quit(s: ChatSession, _args: str) -> None:
    info("goodbye")
    s.close()


COMMANDS = {
    "/help":    cmd_help,
    "/nick":    cmd_nick,
    "/msg":     cmd_msg,
    "/log":     cmd_log,
    "/history": cmd_history,
    "/quit":    cmd_quit,
}


# ---- helpers ------------------------------------------------------------

def _decode_jwt_subject(jwt: str) -> str:
    """Pull `sub` out of a JWT without verifying the signature."""
    import base64
    parts = jwt.split(".")
    if len(parts) != 3:
        raise ValueError("bad jwt")
    pad = "=" * (-len(parts[1]) % 4)
    payload = json.loads(base64.urlsafe_b64decode(parts[1] + pad))
    return str(payload["sub"])


def login_flow(base_url: str) -> dict:
    cached = load_token()
    if cached:
        info(f"using saved token for {cached.get('username')}  (delete {TOKEN_FILE} to force re-login)")
        return cached
    print("Loomic  [L] login  [R] register")
    choice = input("choice: ").strip().lower()
    if choice == "r":
        username = input("username: ").strip()
        email    = input("email: ").strip()
        password = getpass.getpass("password (min 6 chars): ")
        http_register(base_url, username, email, password)
        info(f"account created for {username}, logging in...")
    else:
        username = input("username: ").strip()
        password = getpass.getpass("password: ")
    auth = http_login(base_url, username, password)
    save_token(auth)
    return auth


def main() -> int:
    ap = argparse.ArgumentParser(description="Loomic CLI chat client")
    ap.add_argument("--host", default=os.getenv("LOOMIC_HOST", "127.0.0.1"))
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--ws-scheme",  default="ws",   help="ws or wss")
    ap.add_argument("--http-scheme", default="http", help="http or https")
    args = ap.parse_args()

    port_str = f":{args.port}" if args.port is not None else ""
    base_url = f"{args.http_scheme}://{args.host}{port_str}"
    ws_url   = f"{args.ws_scheme}://{args.host}{port_str}/ws"

    try:
        auth = login_flow(base_url)
    except Exception as e:
        err(str(e)); return 1

    sess = ChatSession(base_url, ws_url, auth)

    try:
        sess.connect()
    except Exception as e:
        err(f"could not connect: {e}"); return 1

    # Prime the active conversation with the most recent one, if any.
    try:
        convos = http_list_conversations(base_url, sess.token)
        if convos:
            sess.active_conv = str(convos[0].get("id"))
            info(f"active conversation: {sess.active_conv}  (use /msg to start a different one)")
    except Exception as e:
        err(f"could not list conversations: {e}")

    signal.signal(signal.SIGINT,  lambda *_: sess.close())
    signal.signal(signal.SIGTERM, lambda *_: sess.close())

    rt = threading.Thread(target=sess.recv_loop, daemon=True)
    rt.start()

    print(HELP_TEXT, end="")
    while not sess.stop.is_set():
        try:
            line = input("> ")
        except (EOFError, KeyboardInterrupt):
            break
        if not line:
            continue
        if line.startswith("/"):
            cmd, _, rest = line.partition(" ")
            handler = COMMANDS.get(cmd)
            if handler is None:
                err(f"unknown command: {cmd}  (try /help)")
                continue
            try:
                handler(sess, rest)
            except Exception as e:
                err(f"command error: {e}")
            continue
        try:
            sess.send_chat(line)
        except Exception as e:
            err(f"send failed: {e}")

    sess.close()
    rt.join(timeout=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
