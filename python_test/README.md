# Loomic TCP E2E Messaging Test

This folder contains `test_e2e_messaging.py`, a Python end-to-end test script for the Loomic messaging flow.

The script verifies the full path below:

1. Register two users over HTTP
2. Log both users in over HTTP
3. Open two TLS TCP connections to the messaging server
4. Authenticate both TCP connections with JWTs
5. Send a chat message from Alice to Bob
6. Confirm Bob receives the message

## File

- `test_e2e_messaging.py`: end-to-end test for HTTP auth + TCP messaging

## Prerequisites

Install Python dependencies:

```bash
pip install requests protobuf
```

Optional: generate protobuf bindings if you want decoded chat payloads instead of raw bytes:

```bash
protoc --python_out=. proto/chat.proto
```

If your generated file is placed elsewhere, make sure `chat_pb2.py` is importable by the script.

## Default Target

The script is currently set up to use:

- HTTP base URL: `http://35.232.85.186:8080`
- TCP port: `7777` (matches `server/include/LoomicServer/util/Config.hpp` and
  `server/config/server.json`; some older drafts of `server/api/openapi.json`
  list 9000 — that value is stale)

## How To Run

From this directory:

```bash
python3 test_e2e_messaging.py --host http://35.232.85.186 --no-verify-tls
```

Why `--no-verify-tls`:

- The TCP connection uses TLS.
- When connecting directly to an IP address, certificate validation often fails unless the certificate matches that IP.
- This flag disables TLS certificate verification for test purposes.

Localhost TLS verification:

- If you run against `localhost` or `127.0.0.1`, the script now looks for a dev CA cert at `certs/server.crt` or `../certs/server.crt`.
- If found, it uses that cert as the trust anchor for HTTPS, WSS, and the TLS TCP socket, so you can keep verification enabled.
- You can also override the cert path with `LOOMIC_TLS_CA_CERT=/path/to/server.crt`.

## Useful Options

Run against a different host:

```bash
python3 test_e2e_messaging.py --host http://YOUR_HOST --no-verify-tls
```

Use a different HTTP port:

```bash
python3 test_e2e_messaging.py --host http://35.232.85.186 --http-port 8080 --no-verify-tls
```

Use a different TCP port:

```bash
python3 test_e2e_messaging.py --host http://35.232.85.186 --tcp-port 7777 --no-verify-tls
```

If your TLS certificate is valid for the host you use, you can omit `--no-verify-tls`.

## Expected Successful Output

On success, you should see output similar to:

```text
[config] HTTP base_url=http://35.232.85.186:8080  TCP host=35.232.85.186  verify_tls=False

── Step 1: Register users ───────────────────────────────────
[http] Registered 'alice_xxxxxx'  id=...
[http] Registered 'bob_xxxxxx'  id=...

── Step 2: Login ────────────────────────────────────────────
[http] Logged in  'alice_xxxxxx'  access_token=...
[http] Logged in  'bob_xxxxxx'  access_token=...

── Step 3: Connect to TCP server ────────────────────────────
[tcp] Alice connected to 35.232.85.186:7777
[tcp] Bob   connected to 35.232.85.186:7777

── Step 4: AUTH handshake ───────────────────────────────────
[alice] AUTH handshake complete
[bob] AUTH handshake complete

── Step 6: Send a message (Alice) ─────────────────────────────
[alice→bob] 'Hello Bob, from Alice!'
[Bob received CHAT] ...
```

## What The Script Does Internally

The test creates two unique users on each run:

- `alice_<random_suffix>`
- `bob_<random_suffix>`

It then:

- calls `POST /auth/register`
- calls `POST /auth/login`
- extracts the user IDs from the JWT payload
- opens TLS sockets to the TCP messaging service
- sends an `AUTH` frame containing each user JWT
- sends a `CHAT` frame from Alice to Bob

## Common Issues

`chat_pb2 not found`

- This is not fatal.
- The test still works.
- Received chat payloads will be printed as raw bytes instead of decoded protobuf fields.

HTTP connection errors

- Confirm the server is reachable on port `8080`.
- Confirm the `/auth/register` and `/auth/login` endpoints are running.

TCP connection errors

- Confirm the messaging server is reachable on port `7777`.
- Confirm the TCP service is configured for TLS.

TLS certificate errors

- Retry with `--no-verify-tls` for test environments using self-signed certs or IP-based access.

AUTH failure

- If the script prints `Server rejected AUTH`, the JWT may be invalid for the TCP service or the TCP server auth handling may be failing.

No message received by Bob

- Check that:
- Alice and Bob both completed the AUTH handshake
- the recipient ID is correct
- the TCP service is routing messages correctly

## Example Command Used Successfully

This command was run successfully against your current server:

```bash
python3 test_e2e_messaging.py --host http://35.232.85.186 --no-verify-tls
```

## See also

- [`cli/`](../cli/) — interactive Python chat client over the WebSocket
  endpoint (port 8080). Useful for poking at a running server by hand
  without the protobuf TCP framing.
