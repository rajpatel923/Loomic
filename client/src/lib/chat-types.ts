export type ChatConnectionState =
  | "idle"
  | "connecting"
  | "connected"
  | "reconnecting"
  | "error"
  | "disconnected";

export type ChatMessageDirection = "incoming" | "outgoing";

export type ChatRecord = {
  id: string;
  senderId: string;
  recipientId: string;
  content: string;
  timestampMs: number;
  direction: ChatMessageDirection;
};

export type ChatConnectionSnapshot = {
  clientSessionId: string;
  state: ChatConnectionState;
  selfUserId: string | null;
  detail: string | null;
  connectedAt: number | null;
  lastMessageAt: number | null;
  lastHeartbeatAt: number | null;
  lastErrorAt: number | null;
  reconnectAttempt: number;
};

export type ChatSnapshotPayload = {
  status: ChatConnectionSnapshot;
  messages: ChatRecord[];
};

export type ChatStatusPayload = {
  status: ChatConnectionSnapshot;
};

export type ChatTransportErrorPayload = {
  message: string;
};
