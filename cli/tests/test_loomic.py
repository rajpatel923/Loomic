"""Unit tests for cli/loomic.py.

Exercises the parts of the CLI that don't need a real server:
  - JWT subject decoding
  - command registry & help text
  - /nick (local rename)
  - /log (reads ~/.loomic/activity.log)
  - /msg flow with mocked HTTP + mocked WebSocket
  - render() for incoming chat / error / pong frames

Run from cli/:  python -m unittest tests.test_loomic
"""

from __future__ import annotations

import base64
import json
import sys
import unittest
from io import StringIO
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import loomic  # noqa: E402


def make_jwt(sub: str) -> str:
    payload = base64.urlsafe_b64encode(
        json.dumps({"sub": sub}).encode()
    ).rstrip(b"=").decode()
    return f"header.{payload}.signature"


class FakeWS:
    """A minimal stand-in for websocket.WebSocket — captures sent frames."""

    def __init__(self):
        self.sent: list[str] = []
        self.closed = False

    def send(self, data: str) -> None:
        self.sent.append(data)

    def close(self) -> None:
        self.closed = True


class JwtDecodeTests(unittest.TestCase):
    def test_extracts_sub(self):
        token = make_jwt("123456789")
        self.assertEqual(loomic._decode_jwt_subject(token), "123456789")

    def test_rejects_malformed_jwt(self):
        with self.assertRaises(ValueError):
            loomic._decode_jwt_subject("not-a-jwt")


class CommandRegistryTests(unittest.TestCase):
    def test_only_basic_commands_registered(self):
        self.assertEqual(
            set(loomic.COMMANDS.keys()),
            {"/help", "/nick", "/msg", "/log", "/quit"},
        )

    def test_help_lists_each_command(self):
        for cmd in loomic.COMMANDS:
            self.assertIn(cmd, loomic.HELP_TEXT)


class NickTests(unittest.TestCase):
    def setUp(self):
        self.sess = self._make_session()

    def _make_session(self):
        with mock.patch.object(loomic, "websocket"):
            sess = loomic.ChatSession(
                base_url="http://x", ws_url="ws://x",
                auth={"access_token": make_jwt("1"), "username": "alice"},
            )
        return sess

    def test_nick_rename(self):
        loomic.cmd_nick(self.sess, "newname")
        self.assertEqual(self.sess.nick, "newname")

    def test_nick_blank_keeps_old_name(self):
        loomic.cmd_nick(self.sess, "")
        self.assertEqual(self.sess.nick, "alice")


class MsgTests(unittest.TestCase):
    def test_msg_sends_to_min_id_conv(self):
        sess = _new_session_with_ws(my_id="100", username="alice")

        with mock.patch.object(loomic, "http_search_user",
                               return_value=[{"id": "200", "username": "bob"}]):
            loomic.cmd_msg(sess, "bob hello there")

        self.assertEqual(len(sess.ws.sent), 1)
        sent = json.loads(sess.ws.sent[0])
        self.assertEqual(sent["type"], "chat")
        self.assertEqual(sent["conv_id"], "100")     # min(100, 200)
        self.assertEqual(sent["content"], "hello there")

    def test_msg_unknown_user_sends_nothing(self):
        sess = _new_session_with_ws(my_id="100", username="alice")
        with mock.patch.object(loomic, "http_search_user", return_value=[]):
            loomic.cmd_msg(sess, "ghost hi")
        self.assertEqual(sess.ws.sent, [])

    def test_msg_usage_error(self):
        sess = _new_session_with_ws(my_id="100", username="alice")
        loomic.cmd_msg(sess, "onlyone")
        self.assertEqual(sess.ws.sent, [])


class LogTests(unittest.TestCase):
    def test_log_prints_recent_activity(self):
        with mock.patch.object(loomic, "ACTIVITY_LOG") as p:
            p.exists.return_value = True
            p.read_text.return_value = "\n".join(f"line-{i}" for i in range(50))
            buf = StringIO()
            with mock.patch("sys.stdout", buf):
                loomic.cmd_log(None, "5")
        out = buf.getvalue()
        self.assertIn("--- last 5 activity entries ---", out)
        self.assertIn("line-49", out)
        self.assertIn("line-45", out)
        self.assertNotIn("line-44", out)


class RenderTests(unittest.TestCase):
    def test_chat_frame_uses_known_username(self):
        sess = _new_session_with_ws(my_id="1", username="alice")
        sess.peers_by_id["200"] = "bob"
        buf = StringIO()
        with mock.patch("sys.stdout", buf):
            sess.render({"type": "chat", "sender_id": "200",
                         "content": "hi", "ts_ms": 1700000000000})
        self.assertIn("<bob> hi", buf.getvalue())

    def test_error_frame_goes_to_stderr(self):
        sess = _new_session_with_ws(my_id="1", username="alice")
        buf = StringIO()
        with mock.patch("sys.stderr", buf):
            sess.render({"type": "error", "message": "oops"})
        self.assertIn("server error", buf.getvalue())

    def test_pong_is_silent(self):
        sess = _new_session_with_ws(my_id="1", username="alice")
        out, errbuf = StringIO(), StringIO()
        with mock.patch("sys.stdout", out), mock.patch("sys.stderr", errbuf):
            sess.render({"type": "pong"})
        self.assertEqual(out.getvalue(), "")
        self.assertEqual(errbuf.getvalue(), "")


class SendChatTests(unittest.TestCase):
    def test_send_chat_uses_active_conv(self):
        sess = _new_session_with_ws(my_id="1", username="alice")
        sess.active_conv = "42"
        sess.send_chat("hello")
        self.assertEqual(json.loads(sess.ws.sent[0]),
                         {"type": "chat", "conv_id": "42", "content": "hello"})

    def test_send_chat_without_active_conv_does_nothing(self):
        sess = _new_session_with_ws(my_id="1", username="alice")
        sess.active_conv = None
        sess.send_chat("hello")
        self.assertEqual(sess.ws.sent, [])


def _new_session_with_ws(my_id: str, username: str) -> loomic.ChatSession:
    sess = loomic.ChatSession(
        base_url="http://x", ws_url="ws://x",
        auth={"access_token": make_jwt(my_id), "username": username},
    )
    sess.ws = FakeWS()
    return sess


if __name__ == "__main__":
    unittest.main(verbosity=2)
