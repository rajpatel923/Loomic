# Backend-to-Frontend Gap Report

Date: 2026-04-24

## Scope

This report compares the backend capabilities implemented under `server/` with the features currently exposed in the shipped Next.js frontend under `client/`.

Assumption: "missing from the frontend" means not available to an end user in the current UI, even if a partial proxy route already exists in `client/src/app/api`.

## What The Frontend Already Exposes

The current frontend supports:

- Account registration and login via `client/src/components/LoginExperience.tsx`.
- Direct-message conversation creation, listing, history loading, and sending from `client/src/components/ChatPlaceholder.tsx`.
- User search for starting a DM via `client/src/app/api/users/search/route.ts`.
- Sign-out via `client/src/components/SimpleChatLayout.tsx`.

The frontend does this through a small set of proxy routes:

- `client/src/app/api/auth/*`
- `client/src/app/api/conversations/*`
- `client/src/app/api/users/search/route.ts`
- `client/src/app/api/chat/socket-config/route.ts`

## Missing Or Partially Missing Backend Features

| Feature | Backend evidence | Current frontend status | Gap |
|---|---|---|---|
| Group chat creation and management | `server/api/openapi.json:873`, `server/api/openapi.json:909`, `server/api/openapi.json:970`, `server/api/openapi.json:994`, `server/src/http/GroupsHandler.cpp:43`, `server/src/http/GroupsHandler.cpp:117`, `server/src/http/GroupsHandler.cpp:188`, `server/src/http/GroupsHandler.cpp:246`, `server/src/http/GroupsHandler.cpp:315` | The chat client explicitly filters conversation results down to DMs only at `client/src/components/ChatPlaceholder.tsx:461`. There are no `/api/groups` routes and no group UI. | Entire group lifecycle is missing from the frontend: create group, rename group, view members, add/remove members, and open group threads. |
| Group messaging over WebSocket | `server/api/openapi.json:1078`, `server/src/ws/WebSocketSession.cpp:179`, `server/src/ws/WebSocketSession.cpp:429` | The send flow only emits `{ type: "chat", conv_id, content }` in `client/src/components/ChatPlaceholder.tsx:1092`. | Even if groups existed in the list, the frontend cannot send `group_chat` frames or render a group-specific thread model. |
| User profile view/edit | `server/api/openapi.json:761`, `server/src/http/UsersHandler.cpp:111`, `server/src/http/UsersHandler.cpp:200` | No client route exists for `/users/{id}`, and there is no profile/settings page. | Backend supports viewing and updating profile data, but the frontend has no profile surface at all. |
| Presence / online status | `server/api/openapi.json:784`, `server/src/http/UsersHandler.cpp:144` | No proxy route or UI consumes presence. Conversation list items only show name, preview, and timestamps. | Users cannot see whether another user is online or when they were last seen. |
| Read/unread state | `server/api/openapi.json:263`, `server/api/openapi.json:811`, `server/src/http/ConversationsHandler.cpp:229`, `server/src/http/ConversationsHandler.cpp:241`, `server/src/http/ReceiptsHandler.cpp:42`, `server/src/ws/WebSocketSession.cpp:183`, `server/src/ws/WebSocketSession.cpp:630` | The frontend conversation model keeps only `convId`, `userId`, and `username` at `client/src/components/ChatPlaceholder.tsx:42` and `client/src/components/ChatPlaceholder.tsx:462`. It never calls the read endpoint and never sends `read` frames. | Unread counts are available in the backend but not shown. Opening a thread does not mark it read through the backend APIs. |
| Delivery receipts and read receipts | `server/api/openapi.json:1078`, `server/src/ws/WebSocketSession.cpp:270`, `server/src/ws/WebSocketSession.cpp:376`, `server/src/ws/WebSocketSession.cpp:381` | The frontend WebSocket payload union only handles `pong`, `error`, and `chat` at `client/src/components/ChatPlaceholder.tsx:101`. Outgoing messages only show a local `Sending...` indicator in `client/src/components/SimpleChatLayout.tsx:289`. | Backend delivery/read receipts are not parsed, stored, or rendered in the UI. |
| Typing indicators | `server/api/openapi.json:1078`, `server/src/ws/WebSocketSession.cpp:181`, `server/src/ws/WebSocketSession.cpp:385`, `server/src/ws/WebSocketSession.cpp:558` | The frontend never sends a typing frame and does not handle incoming `typing` events. | Real-time typing status exists in the backend but is absent from the frontend. |
| Message deletion | `server/api/openapi.json:1011`, `server/src/http/MessagesHandler.cpp:176`, `server/src/http/MessagesHandler.cpp:237`, `server/src/ws/WebSocketSession.cpp:372` | There is no `/api/messages/[msg_id]` route, no delete button in the UI, and no handler for `delete_notify`. | Users cannot delete messages or see live delete updates from other clients. |
| File upload / download attachments | `server/api/openapi.json:1029`, `server/api/openapi.json:1056`, `server/src/http/MessagesHandler.cpp:271`, `server/src/http/MessagesHandler.cpp:316` | No upload proxy, no attachment picker, and no attachment rendering in the chat composer or message list. | Backend file sharing exists but the frontend only supports plain-text messages. |
| Push notification device registration | `server/api/openapi.json:828` | No `/api/push/register` proxy and no notification setup UI. | Web, iOS, or Android device-token registration is not exposed from the frontend. |
| Token refresh flow in practice | `server/api/openapi.json:668`, `client/src/app/api/auth/refresh/route.ts:26` | A proxy route exists, but the app never calls it. Search across `client/src` only shows the route file, not a consumer. Session state is stored in browser storage in `client/src/lib/session.ts:41` and `client/src/lib/session.ts:42`, and the live socket uses the original access token in `client/src/components/ChatPlaceholder.tsx:886`. | Refresh support exists on the backend and in a proxy route, but the shipped frontend behaves like refresh is unimplemented. Expired access tokens likely force a re-login instead of silent renewal. |
| Rich conversation metadata | `server/api/openapi.json:263`, `server/api/openapi.json:278`, `server/api/openapi.json:291`, `server/src/http/ConversationsHandler.cpp:241` | The frontend ignores `unread_count`, `last_msg_preview`, `peer_bio`, `peer_avatar`, `group_name`, and `group_avatar` when it maps API results in `client/src/components/ChatPlaceholder.tsx:462`. | Backend already returns richer list data than the frontend uses, so the current UI underrepresents available conversation state. |

## High-Impact Summary

If the goal is feature parity with the current backend, the biggest missing frontend areas are:

1. Group chat support.
2. Read/unread + delivery/read receipt UX.
3. Typing indicators.
4. Profile and presence surfaces.
5. Attachments and message deletion.
6. Real refresh-token handling.

## Recommended Implementation Order

1. Use the existing conversation metadata fully: unread counts, previews, and mark-as-read behavior.
2. Add receipt + typing event handling to the WebSocket client.
3. Add group conversations end-to-end.
4. Add profile/presence views.
5. Add attachments and message deletion.
6. Finish silent token refresh before sessions start expiring in active use.
