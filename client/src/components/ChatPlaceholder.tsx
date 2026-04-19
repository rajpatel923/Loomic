"use client";

import {
  startTransition,
  useCallback,
  useDeferredValue,
  useEffect,
  useEffectEvent,
  useMemo,
  useRef,
  useState,
} from "react";
import { useRouter } from "next/navigation";

import SimpleChatLayout from "@/components/SimpleChatLayout";
import {
  clearStoredSession,
  readStoredSession,
  type StoredSession,
} from "@/lib/session";

type ConnectionState =
  | "idle"
  | "connecting"
  | "connected"
  | "reconnecting"
  | "error"
  | "disconnected";

type ConnectionSnapshot = {
  state: ConnectionState;
  detail: string | null;
  connectedAt: number | null;
  lastHeartbeatAt: number | null;
  lastMessageAt: number | null;
  reconnectAttempt: number;
};

type ConversationSummary = {
  convId: string;
  userId: string;
  username: string;
};

type ConversationMessage = {
  id: string;
  convId: string;
  senderId: string;
  content: string;
  timestampMs: number;
  direction: "incoming" | "outgoing";
  pending?: boolean;
  pendingVisible?: boolean;
};

type HistoryState = {
  loaded: boolean;
  loading: boolean;
  loadingMore: boolean;
  hasMore: boolean;
  oldestCursor: string | null;
  error: string | null;
};

type UserSearchResult = {
  id: string;
  username: string;
};

type ConversationApiResponse = {
  conv_id: string;
  user_id: string;
  username: string;
};

type CreateConversationResponse = {
  conv_id: string;
  created_at: string;
};

type HistoryApiMessage = {
  msg_id: string;
  sender_id: string;
  recipient_id: string;
  content_b64: string;
  msg_type: number;
  ts_ms: number;
};

type UserSearchResponse = {
  users: UserSearchResult[];
};

type RuntimeSocketConfig = {
  url: string;
};

type WebSocketPayload =
  | {
      type: "pong";
    }
  | {
      type: "error";
      msg?: string;
    }
  | {
      type: "chat";
      msg_id: string;
      conv_id: string;
      sender_id: string;
      content: string;
      ts_ms: number;
    };

const EMPTY_MESSAGES: ConversationMessage[] = [];
const PAGE_SIZE = 50;
const HEARTBEAT_INTERVAL_MS = 20_000;

function createInitialConnection(): ConnectionSnapshot {
  return {
    state: "idle",
    detail: "Preparing the live conversation channel.",
    connectedAt: null,
    lastHeartbeatAt: null,
    lastMessageAt: null,
    reconnectAttempt: 0,
  };
}

function createHistoryState(): HistoryState {
  return {
    loaded: false,
    loading: false,
    loadingMore: false,
    hasMore: false,
    oldestCursor: null,
    error: null,
  };
}

function formatClock(timestampMs: number) {
  return new Intl.DateTimeFormat("en-US", {
    hour: "numeric",
    minute: "2-digit",
  }).format(timestampMs);
}

function formatRelativeTime(timestampMs: number) {
  const minutesAgo = Math.max(0, Math.round((Date.now() - timestampMs) / 60_000));

  if (minutesAgo < 1) {
    return "Now";
  }

  if (minutesAgo === 1) {
    return "1m";
  }

  if (minutesAgo < 60) {
    return `${minutesAgo}m`;
  }

  const hoursAgo = Math.round(minutesAgo / 60);
  if (hoursAgo < 24) {
    return `${hoursAgo}h`;
  }

  const daysAgo = Math.round(hoursAgo / 24);
  return `${daysAgo}d`;
}

function decodeBase64Content(value: string) {
  try {
    const binary = atob(value);
    const bytes = Uint8Array.from(binary, (char) => char.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  } catch {
    return "";
  }
}

function normalizeHistoryMessage(
  message: HistoryApiMessage,
  convId: string,
  selfUserId: string | null,
): ConversationMessage {
  return {
    id: message.msg_id,
    convId,
    senderId: message.sender_id,
    content: decodeBase64Content(message.content_b64),
    timestampMs: message.ts_ms,
    direction: selfUserId === message.sender_id ? "outgoing" : "incoming",
  };
}

function buildOptimisticMessage(
  convId: string,
  senderId: string,
  content: string,
): ConversationMessage {
  return {
    id: `temp-${Date.now()}-${Math.random().toString(16).slice(2, 10)}`,
    convId,
    senderId,
    content,
    timestampMs: Date.now(),
    direction: "outgoing",
    pending: true,
    pendingVisible: true,
  };
}

function isSameDeliveredMessage(
  candidate: ConversationMessage,
  incoming: ConversationMessage,
) {
  return (
    candidate.pending &&
    !incoming.pending &&
    candidate.convId === incoming.convId &&
    candidate.senderId === incoming.senderId &&
    candidate.direction === incoming.direction &&
    candidate.content === incoming.content &&
    Math.abs(candidate.timestampMs - incoming.timestampMs) < 15_000
  );
}

function mergeConversationMessages(
  existing: ConversationMessage[],
  incoming: ConversationMessage[],
) {
  const merged = [...existing];

  for (const message of incoming) {
    const sameIdIndex = merged.findIndex((candidate) => candidate.id === message.id);

    if (sameIdIndex >= 0) {
      merged[sameIdIndex] = {
        ...merged[sameIdIndex],
        ...message,
        pending: message.pending ?? false,
        pendingVisible: message.pendingVisible ?? false,
      };
      continue;
    }

    const optimisticIndex = merged.findIndex((candidate) =>
      isSameDeliveredMessage(candidate, message),
    );

    if (optimisticIndex >= 0) {
      merged[optimisticIndex] = {
        ...message,
        pending: false,
        pendingVisible: false,
      };
      continue;
    }

    merged.push(message);
  }

  return merged.toSorted((left, right) => {
    if (left.timestampMs === right.timestampMs) {
      return left.id.localeCompare(right.id);
    }

    return left.timestampMs - right.timestampMs;
  });
}

function ensureConversationSummary(
  conversations: ConversationSummary[],
  nextConversation: ConversationSummary,
) {
  const existingIndex = conversations.findIndex(
    (conversation) => conversation.convId === nextConversation.convId,
  );

  if (existingIndex < 0) {
    return [...conversations, nextConversation];
  }

  const next = [...conversations];
  next[existingIndex] = {
    ...next[existingIndex],
    ...nextConversation,
  };
  return next;
}

function buildAuthHeaders(accessToken: string) {
  return {
    authorization: `Bearer ${accessToken}`,
  };
}

function dismissPendingIndicator(
  messagesByConversation: Record<string, ConversationMessage[]>,
  convId: string,
  messageId: string,
) {
  const existingMessages = messagesByConversation[convId];

  if (!existingMessages) {
    return messagesByConversation;
  }

  return {
    ...messagesByConversation,
    [convId]: existingMessages.map((message) =>
      message.id === messageId
        ? {
            ...message,
            pendingVisible: false,
          }
        : message,
    ),
  };
}

export default function ChatPlaceholder() {
  const router = useRouter();
  const [session, setSession] = useState<StoredSession | null>(null);
  const [sessionResolved, setSessionResolved] = useState(false);
  const [socketUrl, setSocketUrl] = useState<string | null>(null);
  const [, setConnection] = useState<ConnectionSnapshot>(
    createInitialConnection(),
  );
  const [transportNotice, setTransportNotice] = useState<string | null>(null);
  const [conversations, setConversations] = useState<ConversationSummary[]>([]);
  const [conversationsLoading, setConversationsLoading] = useState(false);
  const [conversationsError, setConversationsError] = useState<string | null>(null);
  const [activeConversationId, setActiveConversationId] = useState("");
  const [messagesByConversation, setMessagesByConversation] = useState<
    Record<string, ConversationMessage[]>
  >({});
  const [historyByConversation, setHistoryByConversation] = useState<
    Record<string, HistoryState>
  >({});
  const [threadSearch, setThreadSearch] = useState("");
  const [userSearchQuery, setUserSearchQuery] = useState("");
  const [userSearchResults, setUserSearchResults] = useState<UserSearchResult[]>([]);
  const [userSearchPending, setUserSearchPending] = useState(false);
  const [userSearchError, setUserSearchError] = useState<string | null>(null);
  const [conversationActionError, setConversationActionError] = useState<
    string | null
  >(null);
  const [creatingConversationId, setCreatingConversationId] = useState<
    string | null
  >(null);
  const [composerValue, setComposerValue] = useState("");
  const [sendError, setSendError] = useState<string | null>(null);
  const [pendingSend, setPendingSend] = useState(false);

  const deferredThreadSearch = useDeferredValue(threadSearch);
  const deferredUserSearchQuery = useDeferredValue(userSearchQuery);

  const socketRef = useRef<WebSocket | null>(null);
  const heartbeatTimerRef = useRef<number | null>(null);
  const reconnectTimerRef = useRef<number | null>(null);
  const reconnectAttemptsRef = useRef(0);
  const preventReconnectRef = useRef(false);
  const historyByConversationRef = useRef(historyByConversation);

  const selfUserId = session?.user_id ?? null;

  const activeConversation = conversations.find(
    (conversation) => conversation.convId === activeConversationId,
  );

  const activeHistoryState = activeConversationId
    ? historyByConversation[activeConversationId] ?? createHistoryState()
    : createHistoryState();

  const activeMessages = activeConversationId
    ? messagesByConversation[activeConversationId] ?? EMPTY_MESSAGES
    : EMPTY_MESSAGES;

  useEffect(() => {
    setSession(readStoredSession());
    setSessionResolved(true);
  }, []);

  useEffect(() => {
    historyByConversationRef.current = historyByConversation;
  }, [historyByConversation]);

  const orderedConversations = useMemo(() => {
    return conversations
      .map((conversation) => {
        const lastMessage =
          messagesByConversation[conversation.convId]?.at(-1) ?? null;

        return {
          ...conversation,
          lastMessage,
        };
      })
      .toSorted((left, right) => {
        const leftTime = left.lastMessage?.timestampMs ?? 0;
        const rightTime = right.lastMessage?.timestampMs ?? 0;
        return rightTime - leftTime;
      });
  }, [conversations, messagesByConversation]);

  const visibleConversations = useMemo(() => {
    const query = deferredThreadSearch.trim().toLowerCase();

    if (!query) {
      return orderedConversations;
    }

    return orderedConversations.filter((conversation) => {
      const preview = conversation.lastMessage?.content.toLowerCase() ?? "";
      return (
        conversation.username.toLowerCase().includes(query) ||
        conversation.userId.toLowerCase().includes(query) ||
        preview.includes(query)
      );
    });
  }, [deferredThreadSearch, orderedConversations]);

  const loadConversations = useCallback(
    async (options?: { preferredConversationId?: string; quiet?: boolean }) => {
      if (!session) {
        return;
      }

      const quiet = options?.quiet ?? false;

      if (!quiet) {
        setConversationsLoading(true);
      }

      setConversationsError(null);

      try {
        const response = await fetch("/api/conversations", {
          method: "GET",
          headers: buildAuthHeaders(session.access_token),
          cache: "no-store",
        });

        const payload = (await response.json().catch(() => null)) as
          | ConversationApiResponse[]
          | { error?: string }
          | null;

        if (!response.ok) {
          throw new Error(
            (payload as { error?: string } | null)?.error ??
              "Unable to load your conversations.",
          );
        }

        const nextConversations = ((payload as ConversationApiResponse[]) ?? []).map(
          (conversation) => ({
            convId: conversation.conv_id,
            userId: conversation.user_id,
            username: conversation.username,
          }),
        );

        startTransition(() => {
          setConversations(nextConversations);
          setActiveConversationId((previous) => {
            if (
              options?.preferredConversationId &&
              nextConversations.some(
                (conversation) =>
                  conversation.convId === options.preferredConversationId,
              )
            ) {
              return options.preferredConversationId;
            }

            if (
              previous &&
              nextConversations.some(
                (conversation) => conversation.convId === previous,
              )
            ) {
              return previous;
            }

            return nextConversations[0]?.convId ?? "";
          });
        });
      } catch (error) {
        if (!quiet) {
          setConversationsError(
            error instanceof Error
              ? error.message
              : "Unable to load your conversations.",
          );
        }
      } finally {
        if (!quiet) {
          setConversationsLoading(false);
        }
      }
    },
    [session],
  );

  const loadConversationHistory = useCallback(
    async (convId: string, mode: "replace" | "prepend" = "replace") => {
      if (!session || !convId) {
        return;
      }

      const currentState =
        historyByConversationRef.current[convId] ?? createHistoryState();
      const beforeCursor =
        mode === "prepend" ? currentState.oldestCursor : null;

      if (mode === "prepend" && (!beforeCursor || currentState.loadingMore)) {
        return;
      }

      if (mode === "replace" && currentState.loading) {
        return;
      }

      setHistoryByConversation((previous) => ({
        ...previous,
        [convId]: {
          ...currentState,
          loading: mode === "replace",
          loadingMore: mode === "prepend",
          error: null,
        },
      }));

      const searchParams = new URLSearchParams({
        limit: String(PAGE_SIZE),
      });

      if (beforeCursor) {
        searchParams.set("before", beforeCursor);
      }

      try {
        const response = await fetch(
          `/api/conversations/${encodeURIComponent(convId)}/messages?${searchParams.toString()}`,
          {
            method: "GET",
            headers: buildAuthHeaders(session.access_token),
            cache: "no-store",
          },
        );

        const payload = (await response.json().catch(() => null)) as
          | HistoryApiMessage[]
          | { error?: string }
          | null;

        if (!response.ok) {
          throw new Error(
            (payload as { error?: string } | null)?.error ??
              "Unable to load this conversation.",
          );
        }

        const serverMessages = ((payload as HistoryApiMessage[]) ?? []).map((message) =>
          normalizeHistoryMessage(message, convId, selfUserId),
        );

        setMessagesByConversation((previous) => ({
          ...previous,
          [convId]: mergeConversationMessages(
            previous[convId] ?? EMPTY_MESSAGES,
            serverMessages,
          ),
        }));

        setHistoryByConversation((previous) => ({
          ...previous,
          [convId]: {
            loaded: true,
            loading: false,
            loadingMore: false,
            hasMore: serverMessages.length === PAGE_SIZE,
            oldestCursor:
              (payload as HistoryApiMessage[]).at(-1)?.msg_id ??
              previous[convId]?.oldestCursor ??
              null,
            error: null,
          },
        }));
      } catch (error) {
        setHistoryByConversation((previous) => ({
          ...previous,
          [convId]: {
            ...currentState,
            loading: false,
            loadingMore: false,
            error:
              error instanceof Error
                ? error.message
                : "Unable to load this conversation.",
          },
        }));
      }
    },
    [selfUserId, session],
  );

  const handleSocketPayload = useEffectEvent((payload: WebSocketPayload) => {
    if (payload.type === "pong") {
      setConnection((previous) => ({
        ...previous,
        state: "connected",
        detail: "Live conversation sync is healthy.",
        lastHeartbeatAt: Date.now(),
        reconnectAttempt: reconnectAttemptsRef.current,
      }));
      return;
    }

    if (payload.type === "error") {
      const message = payload.msg ?? "The messaging service rejected this session.";
      const shouldStopReconnect = /token/i.test(message);

      setTransportNotice(message);
      setConnection((previous) => ({
        ...previous,
        state: "error",
        detail: message,
      }));

      preventReconnectRef.current = shouldStopReconnect;
      socketRef.current?.close();
      return;
    }

    const nextMessage: ConversationMessage = {
      id: payload.msg_id,
      convId: payload.conv_id,
      senderId: payload.sender_id,
      content: payload.content,
      timestampMs: payload.ts_ms,
      direction: payload.sender_id === selfUserId ? "outgoing" : "incoming",
    };

    setMessagesByConversation((previous) => ({
      ...previous,
      [payload.conv_id]: mergeConversationMessages(
        previous[payload.conv_id] ?? EMPTY_MESSAGES,
        [nextMessage],
      ),
    }));

    setConnection((previous) => ({
      ...previous,
      state: "connected",
      detail: "Live conversation sync is active.",
      lastMessageAt: nextMessage.timestampMs,
      reconnectAttempt: reconnectAttemptsRef.current,
    }));

    if (payload.sender_id !== selfUserId) {
      setConversations((previous) =>
        ensureConversationSummary(previous, {
          convId: payload.conv_id,
          userId: payload.sender_id,
          username:
            previous.find((conversation) => conversation.convId === payload.conv_id)
              ?.username ?? `User ${payload.sender_id}`,
        }),
      );

      if (!activeConversationId) {
        startTransition(() => {
          setActiveConversationId(payload.conv_id);
        });
      }

      void loadConversations({
        preferredConversationId: payload.conv_id,
        quiet: true,
      });
    }
  });

  useEffect(() => {
    if (!sessionResolved) {
      return;
    }

    if (!session) {
      router.replace("/");
    }
  }, [router, session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    let disposed = false;

    async function fetchSocketConfig() {
      try {
        const response = await fetch("/api/chat/socket-config", {
          cache: "no-store",
        });

        const payload = (await response.json()) as RuntimeSocketConfig;

        if (!response.ok || !payload.url) {
          throw new Error("Unable to resolve the Loomic WebSocket endpoint.");
        }

        if (!disposed) {
          setSocketUrl(payload.url);
        }
      } catch (error) {
        if (!disposed) {
          setTransportNotice(
            error instanceof Error
              ? error.message
              : "Unable to resolve the Loomic WebSocket endpoint.",
          );
        }
      }
    }

    fetchSocketConfig().catch(() => undefined);

    return () => {
      disposed = true;
    };
  }, [session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    void loadConversations();
  }, [session, loadConversations, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    const query = deferredUserSearchQuery.trim();

    if (query.length < 2) {
      setUserSearchResults([]);
      setUserSearchError(null);
      setUserSearchPending(false);
      return;
    }

    const controller = new AbortController();

    setUserSearchPending(true);
    setUserSearchError(null);

    fetch(`/api/users/search?q=${encodeURIComponent(query)}&limit=8`, {
      method: "GET",
      headers: buildAuthHeaders(session.access_token),
      cache: "no-store",
      signal: controller.signal,
    })
      .then(async (response) => {
        const payload = (await response.json().catch(() => null)) as
          | UserSearchResponse
          | { error?: string }
          | null;

        if (!response.ok) {
          throw new Error(
            (payload as { error?: string } | null)?.error ??
              "Unable to search Loomic users.",
          );
        }

        return (payload as UserSearchResponse).users ?? [];
      })
      .then((users) => {
        setUserSearchResults(users.filter((user) => user.id !== selfUserId));
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) {
          return;
        }

        setUserSearchError(
          error instanceof Error
            ? error.message
            : "Unable to search Loomic users.",
        );
      })
      .finally(() => {
        if (!controller.signal.aborted) {
          setUserSearchPending(false);
        }
      });

    return () => {
      controller.abort();
    };
  }, [deferredUserSearchQuery, selfUserId, session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session || !socketUrl) {
      return;
    }

    let disposed = false;

    const clearHeartbeat = () => {
      if (heartbeatTimerRef.current !== null) {
        window.clearInterval(heartbeatTimerRef.current);
        heartbeatTimerRef.current = null;
      }
    };

    const clearReconnect = () => {
      if (reconnectTimerRef.current !== null) {
        window.clearTimeout(reconnectTimerRef.current);
        reconnectTimerRef.current = null;
      }
    };

    const scheduleReconnect = (detail: string) => {
      if (disposed || preventReconnectRef.current || reconnectTimerRef.current !== null) {
        return;
      }

      reconnectAttemptsRef.current += 1;
      const waitMs = Math.min(15_000, 1_000 * 2 ** (reconnectAttemptsRef.current - 1));

      setConnection((previous) => ({
        ...previous,
        state: "reconnecting",
        detail,
        reconnectAttempt: reconnectAttemptsRef.current,
      }));

      reconnectTimerRef.current = window.setTimeout(() => {
        reconnectTimerRef.current = null;
        openSocket();
      }, waitMs);
    };

    const openSocket = () => {
      if (disposed) {
        return;
      }

      const attempt = reconnectAttemptsRef.current;
      const nextSocket = new WebSocket(socketUrl);
      socketRef.current = nextSocket;

      setConnection((previous) => ({
        ...previous,
        state: attempt > 0 ? "reconnecting" : "connecting",
        detail:
          attempt > 0
            ? "Restoring the live conversation channel."
            : "Authenticating the live conversation channel.",
        reconnectAttempt: attempt,
      }));

      nextSocket.addEventListener("open", () => {
        if (disposed || socketRef.current !== nextSocket) {
          nextSocket.close();
          return;
        }

        clearReconnect();
        clearHeartbeat();
        reconnectAttemptsRef.current = 0;
        preventReconnectRef.current = false;
        setTransportNotice(null);

        nextSocket.send(
          JSON.stringify({
            type: "auth",
            token: session.access_token,
          }),
        );

        heartbeatTimerRef.current = window.setInterval(() => {
          if (nextSocket.readyState === WebSocket.OPEN) {
            nextSocket.send(JSON.stringify({ type: "ping" }));
          }
        }, HEARTBEAT_INTERVAL_MS);

        setConnection((previous) => ({
          ...previous,
          state: "connected",
          detail: "Live conversation sync is active.",
          connectedAt: Date.now(),
          reconnectAttempt: 0,
        }));
      });

      nextSocket.addEventListener("message", (event) => {
        try {
          handleSocketPayload(JSON.parse(event.data) as WebSocketPayload);
        } catch {
          setTransportNotice("Loomic sent a message the browser could not parse.");
        }
      });

      nextSocket.addEventListener("error", () => {
        setTransportNotice(
          "The live conversation channel hit a network issue. Loomic will keep retrying.",
        );
      });

      nextSocket.addEventListener("close", () => {
        if (socketRef.current === nextSocket) {
          socketRef.current = null;
        }

        clearHeartbeat();

        if (disposed || preventReconnectRef.current) {
          return;
        }

        scheduleReconnect("The live conversation channel dropped. Retrying.");
      });
    };

    openSocket();

    return () => {
      disposed = true;
      preventReconnectRef.current = true;
      clearHeartbeat();
      clearReconnect();
      socketRef.current?.close();
      socketRef.current = null;
    };
  }, [session, socketUrl, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session || !activeConversationId) {
      return;
    }

    const historyState =
      historyByConversationRef.current[activeConversationId] ?? createHistoryState();

    if (historyState.loaded || historyState.loading || historyState.error) {
      return;
    }

    void loadConversationHistory(activeConversationId, "replace");
  }, [activeConversationId, loadConversationHistory, session, sessionResolved]);

  function handleSelectConversation(convId: string) {
    startTransition(() => {
      setActiveConversationId(convId);
    });

    const historyState =
      historyByConversationRef.current[convId] ?? createHistoryState();

    if (!historyState.loaded && !historyState.loading && historyState.error) {
      void loadConversationHistory(convId, "replace");
    }
  }

  async function handleCreateConversation(user: UserSearchResult) {
    if (!session) {
      return;
    }

    setConversationActionError(null);

    const existingConversation = conversations.find(
      (conversation) => conversation.userId === user.id,
    );

    if (existingConversation) {
      startTransition(() => {
        setActiveConversationId(existingConversation.convId);
        setUserSearchQuery("");
        setUserSearchResults([]);
      });
      return;
    }

    setCreatingConversationId(user.id);

    try {
      const response = await fetch("/api/conversations", {
        method: "POST",
        headers: {
          ...buildAuthHeaders(session.access_token),
          "content-type": "application/json",
        },
        body: JSON.stringify({
          member_ids: [user.id],
        }),
      });

      const payload = (await response.json().catch(() => null)) as
        | CreateConversationResponse
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to create the conversation.",
        );
      }

      const convId = (payload as CreateConversationResponse).conv_id;

      startTransition(() => {
        setConversations((previous) =>
          ensureConversationSummary(previous, {
            convId,
            userId: user.id,
            username: user.username,
          }),
        );
        setActiveConversationId(convId);
        setUserSearchQuery("");
        setUserSearchResults([]);
      });

      void loadConversationHistory(convId, "replace");
    } catch (error) {
      setConversationActionError(
        error instanceof Error
          ? error.message
          : "Unable to create the conversation.",
      );
    } finally {
      setCreatingConversationId(null);
    }
  }

  async function handleLoadOlderMessages() {
    if (!activeConversationId) {
      return;
    }

    await loadConversationHistory(activeConversationId, "prepend");
  }

  async function handleSend(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setSendError(null);

    if (!session || !activeConversationId || !activeConversation) {
      setSendError("Choose a conversation before sending.");
      return;
    }

    const socket = socketRef.current;

    if (!socket || socket.readyState !== WebSocket.OPEN) {
      setSendError("The live conversation channel is offline right now.");
      return;
    }

    const content = composerValue.trim();

    if (!content) {
      setSendError("Write a message before sending.");
      return;
    }

    const optimisticMessage = buildOptimisticMessage(
      activeConversationId,
      selfUserId ?? "0",
      content,
    );

    setPendingSend(true);

    try {
      socket.send(
        JSON.stringify({
          type: "chat",
          conv_id: activeConversationId,
          content,
        }),
      );

      setMessagesByConversation((previous) => ({
        ...previous,
        [activeConversationId]: mergeConversationMessages(
          previous[activeConversationId] ?? EMPTY_MESSAGES,
          [optimisticMessage],
        ),
      }));

      window.setTimeout(() => {
        setMessagesByConversation((previous) =>
          dismissPendingIndicator(
            previous,
            activeConversationId,
            optimisticMessage.id,
          ),
        );
      }, 1200);

      setConnection((previous) => ({
        ...previous,
        lastMessageAt: optimisticMessage.timestampMs,
        detail: `Delivered toward ${activeConversation.username}.`,
      }));
      setComposerValue("");
    } catch (error) {
      setSendError(
        error instanceof Error
          ? error.message
          : "Unable to send the Loomic message.",
      );
    } finally {
      setPendingSend(false);
    }
  }

  async function handleSignOut() {
    preventReconnectRef.current = true;
    reconnectAttemptsRef.current = 0;

    if (reconnectTimerRef.current !== null) {
      window.clearTimeout(reconnectTimerRef.current);
      reconnectTimerRef.current = null;
    }

    if (heartbeatTimerRef.current !== null) {
      window.clearInterval(heartbeatTimerRef.current);
      heartbeatTimerRef.current = null;
    }

    socketRef.current?.close();
    socketRef.current = null;

    if (session) {
      await fetch("/api/auth/logout", {
        method: "POST",
        headers: {
          ...buildAuthHeaders(session.access_token),
          "content-type": "application/json",
        },
        body: JSON.stringify({
          refresh_token: session.refresh_token,
        }),
      }).catch(() => undefined);
    }

    clearStoredSession();
    setSession(null);
    router.replace("/");
  }

  if (!sessionResolved || !session) {
    return (
      <section className="section-fade flex h-full min-h-[100dvh] items-center justify-center px-4 py-6">
        <div className="surface-panel w-full max-w-md rounded-[28px] px-6 py-8 text-center">
          <p className="text-sm text-[var(--muted)]">Loading...</p>
        </div>
      </section>
    );
  }

  return (
    <SimpleChatLayout
      activeConversationId={activeConversationId}
      activeConversationName={activeConversation?.username ?? null}
      activeHistoryHasMore={activeHistoryState.hasMore}
      activeHistoryLoading={activeHistoryState.loading}
      activeHistoryLoadingMore={activeHistoryState.loadingMore}
      activeMessages={activeMessages}
      composerValue={composerValue}
      conversationActionError={conversationActionError}
      conversations={visibleConversations}
      conversationsError={conversationsError}
      conversationsLoading={conversationsLoading}
      creatingConversationId={creatingConversationId}
      formatClock={formatClock}
      formatRelativeTime={formatRelativeTime}
      historyError={activeHistoryState.error}
      onComposerChange={setComposerValue}
      onCreateConversation={(user) => {
        void handleCreateConversation(user);
      }}
      onLoadOlderMessages={() => {
        void handleLoadOlderMessages();
      }}
      onRefreshConversations={() => {
        void loadConversations();
      }}
      onSelectConversation={handleSelectConversation}
      onSend={handleSend}
      onSignOut={() => {
        void handleSignOut();
      }}
      onThreadSearchChange={setThreadSearch}
      onUserSearchChange={setUserSearchQuery}
      pendingSend={pendingSend}
      sendError={sendError}
      sessionUsername={session.username}
      showUserSearchResults={
        userSearchPending ||
        Boolean(userSearchError) ||
        Boolean(conversationActionError) ||
        deferredUserSearchQuery.trim().length >= 2
      }
      threadSearch={threadSearch}
      transportNotice={transportNotice}
      userSearchError={userSearchError}
      userSearchPending={userSearchPending}
      userSearchQuery={userSearchQuery}
      userSearchResults={userSearchResults}
    />
  );
}
