# Loomic — Next.js Frontend Integration Guide

This document describes how a Next.js frontend connects to the Loomic backend in its current state.

---

## Architecture Overview

The backend exposes **two separate servers**:

| Server | Protocol | Port | Purpose |
|--------|----------|------|---------|
| HTTP REST API + WebSocket | HTTP (Boost Beast) | `8080` | Auth, conversations, user search, health, real-time chat |
| Real-time Chat (native) | **TLS TCP** (binary) | `9000` | Live messaging for native clients only |

> **Browser integration:** Use the **WebSocket endpoint at `ws://host:8080/ws`** for real-time chat. No relay, no proxy, no binary encoding required — it speaks plain JSON text frames. The TLS TCP port (9000) is for native clients only.

---

## CORS

All endpoints on port 8080 now return CORS headers:

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
```

`OPTIONS` preflight requests return `204 No Content` immediately.

**You no longer need Next.js rewrites or a proxy for CORS.** You can call `http://localhost:8080` directly from the browser. A Next.js proxy is still a valid approach if you want to hide the backend URL or use relative paths, but it is not required.

---

## Snowflake ID precision

All Snowflake IDs (user IDs, message IDs, conversation IDs) are now serialized as **decimal strings** in every JSON response. This avoids JavaScript `number` precision loss above 2^53.

```ts
// IDs come back as strings — no BigInt handling needed
const { id } = await res.json(); // id is "7816523049238528"
```

---

## 1. Auth Endpoints

### `POST /auth/register`
```ts
const res = await fetch('http://localhost:8080/auth/register', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    username: 'alice',
    email: 'alice@example.com',
    password: 'supersecret123',
  }),
});
// 201: { id: "7816523049238528", username: "alice" }
// 409: { error: "username or email already taken" }
```

### `POST /auth/login`
```ts
const res = await fetch('http://localhost:8080/auth/login', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ username: 'alice', password: 'supersecret123' }),
});
// 200: { access_token: "eyJ...", refresh_token: "abc123...", token_type: "Bearer" }
```

Store `access_token` (24h) and `refresh_token` (30d). Prefer `httpOnly` cookies set via a Next.js server action.

### `POST /auth/refresh`
```ts
const res = await fetch('http://localhost:8080/auth/refresh', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ refresh_token: storedRefreshToken }),
});
// 200: { access_token: "eyJ...", refresh_token: "new_token...", token_type: "Bearer" }
// Old refresh token is immediately invalidated.
```

### `POST /auth/logout`
```ts
const res = await fetch('http://localhost:8080/auth/logout', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${accessToken}`,
  },
  body: JSON.stringify({ refresh_token: storedRefreshToken }),
});
// 204: token invalidated (idempotent — 204 even if token was already gone)
```

---

## 2. Conversations

### `POST /conversations` — Create a conversation
```ts
const res = await fetch('http://localhost:8080/conversations', {
  method: 'POST',
  headers: {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${accessToken}`,
  },
  body: JSON.stringify({
    member_ids: ['7816523049238200'], // other user's ID as a string
  }),
});
// 201: { conv_id: "7816523049238900", created_at: "2026-04-15T12:00:00Z" }
```

### `GET /conversations` — List conversations
```ts
const res = await fetch('http://localhost:8080/conversations', {
  headers: { Authorization: `Bearer ${accessToken}` },
});
// 200: [{ conv_id: "...", user_id: "...", username: "bob" }, ...]
// One entry per other-member in each conversation, ordered newest-first.
```

```ts
type ConversationEntry = {
  conv_id:  string;  // Snowflake conversation ID
  user_id:  string;  // Snowflake user ID of the other member
  username: string;
};
```

### `GET /conversations/{id}/messages` — Message history
```ts
const res = await fetch(
  `http://localhost:8080/conversations/${convId}/messages?limit=50&before=${beforeMsgId}`,
  {
    headers: { Authorization: `Bearer ${accessToken}` },
  }
);
// 200: Array of messages, newest first
```

```ts
type MessageResponse = {
  msg_id:       string;          // Snowflake ID as string
  sender_id:    string;          // Snowflake ID as string
  recipient_id: string;          // Snowflake ID as string
  content_b64:  string;          // Base64-encoded message text
  msg_type:     0 | 1 | 2 | 3;  // 0=CHAT
  ts_ms:        number;          // Unix timestamp ms (safe as number)
};

// Decode content:
const text = atob(msg.content_b64);
```

Paginate by passing the `msg_id` of the oldest message in the current page as `before`.

---

## 3. User Search

### `GET /users/search?q=<prefix>&limit=<n>`
```ts
const res = await fetch(
  `http://localhost:8080/users/search?q=${encodeURIComponent(query)}&limit=10`,
  { headers: { Authorization: `Bearer ${accessToken}` } }
);
// 200: { users: [{ id: "7816523049238200", username: "bob" }, ...] }
// q must be >= 2 chars; limit clamped to [1, 20], default 10
```

---

## 4. Real-time Messaging — WebSocket

Connect to `ws://localhost:8080/ws` using the browser's native `WebSocket` API. All frames are JSON text — no binary encoding required.

### Connection flow

```ts
const ws = new WebSocket('ws://localhost:8080/ws');

ws.onopen = () => {
  // Step 1: authenticate immediately after connecting
  ws.send(JSON.stringify({ type: 'auth', token: accessToken }));
};

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);

  if (msg.type === 'chat') {
    // Incoming message
    console.log(msg.sender_id, msg.content, msg.ts_ms);
  } else if (msg.type === 'pong') {
    // Heartbeat ack
  } else if (msg.type === 'error') {
    console.error('ws error:', msg.msg);
  }
};
```

### Send a message
```ts
ws.send(JSON.stringify({
  type:    'chat',
  conv_id: '7816523049238900',  // string
  content: 'Hello!',
}));
```

### Heartbeat — keep connection alive
```ts
// Server closes the connection after 30 seconds without a ping
const heartbeat = setInterval(() => {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'ping' }));
  }
}, 25_000);

ws.onclose = () => clearInterval(heartbeat);
```

### Incoming message shape
```ts
type WsIncomingChat = {
  type:      'chat';
  msg_id:    string;    // Snowflake ID as string
  conv_id:   string;    // Snowflake ID as string
  sender_id: string;    // Snowflake ID as string
  content:   string;    // Plain text (not base64)
  ts_ms:     number;
};
```

> **Note:** Content in WebSocket messages is plain text. Content in `GET /conversations/{id}/messages` is Base64-encoded (`content_b64`). Use `atob()` to decode the REST response.

### Error handling
```ts
ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type === 'error') {
    // e.g. { type: "error", msg: "invalid token" } — server will close after auth failure
  }
};
```

---

## 5. Token Management (App Router)

```ts
// app/actions/auth.ts
'use server';
import { cookies } from 'next/headers';

export async function login(username: string, password: string) {
  const res = await fetch('http://localhost:8080/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error);

  cookies().set('access_token',  data.access_token,  { httpOnly: true, secure: true, maxAge: 86400 });
  cookies().set('refresh_token', data.refresh_token, { httpOnly: true, secure: true, maxAge: 2592000 });
}

export async function logout(accessToken: string, refreshToken: string) {
  await fetch('http://localhost:8080/auth/logout', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${accessToken}` },
    body: JSON.stringify({ refresh_token: refreshToken }),
  });
  cookies().delete('access_token');
  cookies().delete('refresh_token');
}
```

Pass the access token to the WebSocket from a server action or session:

```ts
// Client component — get token from server then open WS
const { accessToken } = await getSession(); // your server-side session helper
const ws = new WebSocket('ws://localhost:8080/ws');
ws.onopen = () => ws.send(JSON.stringify({ type: 'auth', token: accessToken }));
```

---

## 6. Rate Limits

| Layer | Limit |
|-------|-------|
| WebSocket CHAT frames | 5 msg/sec sustained, burst of 20 |
| Exceeded | Server sends `{"type":"error","msg":"rate limited"}`; connection stays open |

---

## 7. Dev Checklist

- [ ] Run PostgreSQL migration `V3__create_conv_members.sql`
- [ ] Run Cassandra migration `V4__messages_conv_schema.cql`
- [ ] Run PostgreSQL migration `V5__create_conversations.sql` (adds `conversations` table + FK)
- [ ] Connect WebSocket to `ws://localhost:8080/ws` — no relay needed
- [ ] Send `{"type":"auth","token":"<jwt>"}` as first WebSocket frame
- [ ] Send `{"type":"ping"}` every ~25 seconds to keep the connection alive
- [ ] Store tokens in `httpOnly` cookies via server actions, not `localStorage`
- [ ] All IDs are strings — no BigInt handling needed
- [ ] Use `atob(msg.content_b64)` to decode message content from the REST history endpoint
