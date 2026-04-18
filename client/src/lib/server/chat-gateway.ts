import "server-only";

import { EventEmitter } from "node:events";
import tls from "node:tls";

import {
  appendFrameChunk,
  decodeChatMessage,
  encodeChatMessageContent,
  encodeFrame,
  extractFrames,
  type ParsedWireFrame,
  WireMessageType,
} from "@/lib/chat-protocol";
import type {
  ChatConnectionSnapshot,
  ChatConnectionState,
  ChatRecord,
  ChatSnapshotPayload,
  ChatStatusPayload,
  ChatTransportErrorPayload,
} from "@/lib/chat-types";
import { decodeJwtSubject } from "@/lib/jwt";
import {
  getChatPort,
  getChatServerName,
  getTcpHost,
  shouldVerifyChatTls,
} from "@/lib/loomic";

type ChatGatewayEvent =
  | {
      event: "status";
      payload: ChatStatusPayload;
    }
  | {
      event: "message";
      payload: ChatRecord;
    }
  | {
      event: "transport-error";
      payload: ChatTransportErrorPayload;
    };

const MAX_BUFFERED_MESSAGES = 200;
const HEARTBEAT_INTERVAL_MS = 20_000;
const IDLE_SHUTDOWN_MS = 2 * 60_000;

function now() {
  return Date.now();
}

function createSnapshot(clientSessionId: string): ChatConnectionSnapshot {
  return {
    clientSessionId,
    state: "idle",
    selfUserId: null,
    detail: null,
    connectedAt: null,
    lastMessageAt: null,
    lastHeartbeatAt: null,
    lastErrorAt: null,
    reconnectAttempt: 0,
  };
}

function createEventEmitter() {
  const emitter = new EventEmitter();
  emitter.setMaxListeners(50);
  return emitter;
}

class ChatGatewayConnection {
  private readonly emitter = createEventEmitter();
  private readonly bufferedMessages: ChatRecord[] = [];
  private readonly snapshot: ChatConnectionSnapshot;
  private readonly clientSessionId: string;

  private accessToken = "";
  private connectPromise: Promise<void> | null = null;
  private heartbeatTimer: NodeJS.Timeout | null = null;
  private idleTimer: NodeJS.Timeout | null = null;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private reconnectAttempt = 0;
  private socket: tls.TLSSocket | null = null;
  private streamBuffer = new Uint8Array(0);
  private userInitiatedDisconnect = false;

  constructor(clientSessionId: string) {
    this.clientSessionId = clientSessionId;
    this.snapshot = createSnapshot(clientSessionId);
  }

  getSnapshot() {
    return { ...this.snapshot };
  }

  getBufferedMessages() {
    return [...this.bufferedMessages];
  }

  hasListeners() {
    return this.emitter.listenerCount("event") > 0;
  }

  subscribe(listener: (event: ChatGatewayEvent) => void) {
    this.cancelIdleShutdown();
    this.emitter.on("event", listener);

    return () => {
      this.emitter.off("event", listener);

      if (!this.hasListeners()) {
        this.scheduleIdleShutdown();
      }
    };
  }

  async ensureConnected(accessToken: string) {
    this.accessToken = accessToken;
    this.snapshot.selfUserId = decodeJwtSubject(accessToken);
    this.userInitiatedDisconnect = false;
    this.cancelIdleShutdown();

    if (this.socket && !this.socket.destroyed) {
      return;
    }

    if (this.connectPromise) {
      return this.connectPromise;
    }

    const nextState: ChatConnectionState =
      this.reconnectAttempt > 0 ? "reconnecting" : "connecting";
    this.setStatus(nextState, "Opening the secure Loomic message tunnel.");

    this.connectPromise = this.openSocket();

    try {
      await this.connectPromise;
    } finally {
      this.connectPromise = null;
    }
  }

  async sendMessage(recipientId: string, content: string) {
    await this.ensureConnected(this.accessToken);

    const socket = this.socket;

    if (!socket || socket.destroyed) {
      throw new Error("The secure chat tunnel is unavailable.");
    }

    const payload = encodeChatMessageContent(content);
    const frame = encodeFrame({
      msgType: WireMessageType.CHAT,
      payload,
      recipientId,
    });

    await this.writeBytes(frame);

    const outgoingMessage: ChatRecord = {
      id: `local-${now()}-${Math.random().toString(16).slice(2, 10)}`,
      senderId: this.snapshot.selfUserId ?? "0",
      recipientId,
      content,
      timestampMs: now(),
      direction: "outgoing",
    };

    this.pushMessage(outgoingMessage);
    this.emit({
      event: "message",
      payload: outgoingMessage,
    });

    return outgoingMessage;
  }

  disconnect() {
    this.userInitiatedDisconnect = true;
    this.clearReconnectTimer();
    this.stopHeartbeat();
    this.cancelIdleShutdown();
    this.streamBuffer = new Uint8Array(0);

    if (this.socket && !this.socket.destroyed) {
      this.socket.destroy();
    }

    this.socket = null;
    this.reconnectAttempt = 0;
    this.setStatus("disconnected", "Signed out of the messaging bridge.");
  }

  private async openSocket() {
    const host = getTcpHost();
    const port = getChatPort();
    const rejectUnauthorized = shouldVerifyChatTls();
    const servername = getChatServerName(host);

    await new Promise<void>((resolve, reject) => {
      let settled = false;

      const socket = tls.connect({
        host,
        port,
        rejectUnauthorized,
        servername,
      });

      const fail = (error: Error) => {
        if (!settled) {
          settled = true;
          reject(error);
        }
      };

      socket.once("secureConnect", async () => {
        this.socket = socket;
        this.streamBuffer = new Uint8Array(0);

        try {
          await this.writeBytes(
            encodeFrame({
              msgType: WireMessageType.AUTH,
              payload: encodeChatMessageContent(this.accessToken),
            }),
            socket,
          );

          this.reconnectAttempt = 0;
          this.startHeartbeat();
          this.setStatus("connected", "Live over Loomic TLS messaging.");

          if (!settled) {
            settled = true;
            resolve();
          }
        } catch (error) {
          socket.destroy();
          fail(
            error instanceof Error
              ? error
              : new Error("Unable to authenticate the Loomic TCP session."),
          );
        }
      });

      socket.on("data", (chunk) => {
        this.handleIncomingBytes(new Uint8Array(chunk));
      });

      socket.on("error", (error) => {
        this.snapshot.lastErrorAt = now();
        this.snapshot.detail = error.message;
        this.emit({
          event: "transport-error",
          payload: {
            message: error.message,
          },
        });
        fail(error);
      });

      socket.on("close", () => {
        this.stopHeartbeat();
        this.socket = null;

        if (this.userInitiatedDisconnect) {
          return;
        }

        this.snapshot.connectedAt = null;
        this.snapshot.detail = "Connection dropped. Retrying automatically.";
        this.snapshot.lastErrorAt = now();
        this.scheduleReconnect();

        if (!settled) {
          settled = true;
          reject(new Error("The Loomic TCP connection closed during setup."));
        }
      });
    });
  }

  private handleIncomingBytes(chunk: Uint8Array) {
    this.streamBuffer = appendFrameChunk(this.streamBuffer, chunk);
    const parsed = extractFrames(this.streamBuffer);
    this.streamBuffer = parsed.remainder;

    for (const frame of parsed.frames) {
      this.handleFrame(frame);
    }
  }

  private handleFrame(frame: ParsedWireFrame) {
    if (frame.msgType === WireMessageType.PONG) {
      this.snapshot.lastHeartbeatAt = now();
      this.snapshot.detail = "Live over Loomic TLS messaging.";
      this.emitStatus();
      return;
    }

    if (frame.msgType === WireMessageType.ERROR) {
      const detail =
        "The messaging server rejected the last operation. Check your token or slow down your send rate.";
      this.snapshot.lastErrorAt = now();
      this.setStatus("error", detail);
      this.emit({
        event: "transport-error",
        payload: {
          message: detail,
        },
      });

      if (this.socket && !this.socket.destroyed) {
        this.socket.destroy();
      }

      return;
    }

    if (frame.msgType !== WireMessageType.CHAT) {
      return;
    }

    const direction =
      frame.senderId === this.snapshot.selfUserId ? "outgoing" : "incoming";
    const decoded = decodeChatMessage(frame.payload, direction);

    if (!decoded) {
      return;
    }

    this.pushMessage(decoded);
    this.emit({
      event: "message",
      payload: decoded,
    });
  }

  private pushMessage(message: ChatRecord) {
    this.snapshot.lastMessageAt = message.timestampMs;

    const nextMessages = this.bufferedMessages.filter(
      (candidate) => candidate.id !== message.id,
    );
    nextMessages.push(message);

    if (nextMessages.length > MAX_BUFFERED_MESSAGES) {
      nextMessages.splice(0, nextMessages.length - MAX_BUFFERED_MESSAGES);
    }

    this.bufferedMessages.splice(0, this.bufferedMessages.length, ...nextMessages);
  }

  private emit(event: ChatGatewayEvent) {
    this.emitter.emit("event", event);
  }

  private emitStatus() {
    this.emit({
      event: "status",
      payload: {
        status: this.getSnapshot(),
      },
    });
  }

  private setStatus(state: ChatConnectionState, detail: string | null) {
    this.snapshot.state = state;
    this.snapshot.detail = detail;
    this.snapshot.reconnectAttempt = this.reconnectAttempt;

    if (state === "connected") {
      this.snapshot.connectedAt = now();
    }

    this.emitStatus();
  }

  private async writeBytes(bytes: Uint8Array, socketOverride?: tls.TLSSocket) {
    const socket = socketOverride ?? this.socket;

    if (!socket || socket.destroyed) {
      throw new Error("The secure chat socket is not connected.");
    }

    await new Promise<void>((resolve, reject) => {
      socket.write(bytes, (error) => {
        if (error) {
          reject(error);
          return;
        }

        resolve();
      });
    });
  }

  private startHeartbeat() {
    this.stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      if (!this.socket || this.socket.destroyed) {
        return;
      }

      this.writeBytes(
        encodeFrame({
          msgType: WireMessageType.PING,
        }),
      ).catch((error) => {
        this.snapshot.lastErrorAt = now();
        this.snapshot.detail =
          error instanceof Error ? error.message : "Unable to send a heartbeat.";
        this.emitStatus();
      });
    }, HEARTBEAT_INTERVAL_MS);
  }

  private stopHeartbeat() {
    if (!this.heartbeatTimer) {
      return;
    }

    clearInterval(this.heartbeatTimer);
    this.heartbeatTimer = null;
  }

  private scheduleReconnect() {
    if (this.userInitiatedDisconnect || this.reconnectTimer) {
      return;
    }

    this.reconnectAttempt += 1;
    this.snapshot.reconnectAttempt = this.reconnectAttempt;
    this.setStatus("reconnecting", "Connection dropped. Retrying automatically.");

    const waitMs = Math.min(15_000, 1_000 * 2 ** (this.reconnectAttempt - 1));
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;

      if (!this.accessToken || this.userInitiatedDisconnect) {
        return;
      }

      this.ensureConnected(this.accessToken).catch((error) => {
        this.snapshot.lastErrorAt = now();
        this.snapshot.detail =
          error instanceof Error
            ? error.message
            : "Unable to restore the secure chat tunnel.";
        this.emitStatus();
      });
    }, waitMs);
  }

  private clearReconnectTimer() {
    if (!this.reconnectTimer) {
      return;
    }

    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
  }

  private scheduleIdleShutdown() {
    this.cancelIdleShutdown();

    this.idleTimer = setTimeout(() => {
      if (this.hasListeners()) {
        return;
      }

      this.disconnect();
    }, IDLE_SHUTDOWN_MS);
  }

  private cancelIdleShutdown() {
    if (!this.idleTimer) {
      return;
    }

    clearTimeout(this.idleTimer);
    this.idleTimer = null;
  }
}

class ChatGatewayManager {
  private readonly connections = new Map<string, ChatGatewayConnection>();

  getConnection(clientSessionId: string) {
    let connection = this.connections.get(clientSessionId);

    if (!connection) {
      connection = new ChatGatewayConnection(clientSessionId);
      this.connections.set(clientSessionId, connection);
    }

    return connection;
  }

  destroyConnection(clientSessionId: string) {
    const connection = this.connections.get(clientSessionId);

    if (!connection) {
      return;
    }

    connection.disconnect();
    this.connections.delete(clientSessionId);
  }

  buildSnapshot(clientSessionId: string): ChatSnapshotPayload {
    const connection = this.getConnection(clientSessionId);

    return {
      status: connection.getSnapshot(),
      messages: connection.getBufferedMessages(),
    };
  }
}

declare global {
  var __loomicChatGatewayManager: ChatGatewayManager | undefined;
}

export function getChatGatewayManager() {
  if (!globalThis.__loomicChatGatewayManager) {
    globalThis.__loomicChatGatewayManager = new ChatGatewayManager();
  }

  return globalThis.__loomicChatGatewayManager;
}
