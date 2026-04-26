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
import Image from "next/image";
import { useRouter } from "next/navigation";

import { decodeJwtExpiry } from "@/lib/jwt";
import {
  clearStoredSession,
  readStoredSession,
  updateStoredSessionTokens,
  type StoredSession,
} from "@/lib/session";

type ConversationKind = "dm" | "group";
type MessageReceiptStatus = "sending" | "sent" | "delivered" | "read";
type DetailsPanelMode = "none" | "settings" | "profile" | "group";

type ConversationSummary = {
  convId: string;
  kind: ConversationKind;
  title: string;
  peerId: string | null;
  peerBio: string;
  peerAvatar: string;
  groupAvatar: string;
  unreadCount: number;
  lastPreview: string;
  lastActivityAt: string;
};

type ConversationMessage = {
  id: string;
  convId: string;
  senderId: string;
  recipientId: string;
  content: string;
  timestampMs: number;
  direction: "incoming" | "outgoing";
  pending?: boolean;
  pendingVisible?: boolean;
  receiptStatus?: MessageReceiptStatus;
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
  avatar_url?: string;
};

type UserProfile = {
  id: string;
  username: string;
  bio: string;
  avatar_url: string;
};

type PresenceState = {
  user_id: string;
  online: boolean;
  server_id: string | null;
  last_seen_at: number | null;
};

type GroupMember = {
  user_id: string;
  username: string;
  role: "admin" | "member";
};

type GroupDetails = {
  group_id: string;
  name: string;
  creator_id: string;
  created_at: string;
  members: GroupMember[];
};

type ConversationApiResponse = {
  kind: ConversationKind;
  id: string;
  last_activity_at: string;
  last_msg_preview: string;
  unread_count: number;
  peer_id?: string;
  peer_name?: string;
  peer_bio?: string;
  peer_avatar?: string;
  group_name?: string;
  group_avatar?: string;
};

type CreateConversationResponse = {
  conv_id: string;
  created_at: string;
};

type CreateGroupResponse = {
  group_id: string;
  name: string;
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

type RefreshResponse = {
  access_token: string;
  refresh_token: string;
  token_type: string;
};

type UploadResponse = {
  url: string;
};

type GroupMemberMutationResponse = {
  group_id: string;
  user_id: string;
};

type GroupRenameResponse = {
  group_id: string;
  name: string;
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
      group_id?: string;
    }
  | {
      type: "delivered";
      msg_id: string;
      conv_id: string;
      user_id: string;
    }
  | {
      type: "read";
      conv_id: string;
      user_id: string;
      ts_ms?: number;
    }
  | {
      type: "typing";
      conv_id: string;
      user_id: string;
      is_group?: boolean;
    }
  | {
      type: "delete_notify";
      msg_id: string;
      conv_id: string;
    };

const EMPTY_MESSAGES: ConversationMessage[] = [];
const PAGE_SIZE = 50;
const HEARTBEAT_INTERVAL_MS = 20_000;
const TOKEN_REFRESH_BUFFER_MS = 5 * 60_000;
const SECONDARY_REQUEST_COOLDOWN_MS = 30_000;
const READ_EMIT_THROTTLE_MS = 2_000;

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

function formatIsoDate(value: string | null | undefined) {
  if (!value) {
    return "No activity yet";
  }

  const timestamp = Date.parse(value);
  if (Number.isNaN(timestamp)) {
    return "No activity yet";
  }

  return formatRelativeTime(timestamp);
}

function formatPresence(presence: PresenceState | null | undefined) {
  if (!presence) {
    return "Presence unavailable";
  }

  if (presence.online) {
    return "Online now";
  }

  if (!presence.last_seen_at) {
    return "Offline";
  }

  return `Last seen ${formatRelativeTime(presence.last_seen_at)}`;
}

function getDisplayName(value: string | null | undefined, fallback = "Loomic Member") {
  const normalized = value?.trim();
  return normalized ? normalized : fallback;
}

function getInitials(value: string | null | undefined) {
  const parts = getDisplayName(value, "LM").split(/\s+/).filter(Boolean);

  if (parts.length === 0) {
    return "LM";
  }

  return parts
    .slice(0, 2)
    .map((part) => part[0]?.toUpperCase() ?? "")
    .join("")
    .slice(0, 2);
}

function getAvatarUrl(value: string | null | undefined) {
  const normalized = value?.trim();
  return normalized ? normalized : null;
}

function Avatar({
  alt,
  className,
  name,
  src,
}: {
  alt?: string;
  className: string;
  name: string | null | undefined;
  src?: string | null;
}) {
  const avatarUrl = getAvatarUrl(src);

  return (
    <div
      className={`relative shrink-0 overflow-hidden rounded-full bg-[rgba(255,255,255,0.08)] text-[var(--foreground)] ${className}`}
    >
      <span className="flex h-full w-full items-center justify-center">
        {getInitials(name)}
      </span>
      {avatarUrl ? (
        <Image
          alt={alt ?? ""}
          className="absolute inset-0 h-full w-full object-cover"
          fill
          key={avatarUrl}
          onError={(event) => {
            event.currentTarget.style.display = "none";
          }}
          sizes="56px"
          src={avatarUrl}
          unoptimized
        />
      ) : null}
    </div>
  );
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

function normalizeConversation(conversation: ConversationApiResponse): ConversationSummary {
  if (conversation.kind === "group") {
    return {
      convId: conversation.id,
      kind: "group",
      title: getDisplayName(conversation.group_name, "Untitled Group"),
      peerId: null,
      peerBio: "",
      peerAvatar: "",
      groupAvatar: conversation.group_avatar ?? "",
      unreadCount: conversation.unread_count ?? 0,
      lastPreview: conversation.last_msg_preview ?? "",
      lastActivityAt: conversation.last_activity_at ?? "",
    };
  }

  return {
    convId: conversation.id,
    kind: "dm",
    title: getDisplayName(conversation.peer_name, "Unknown User"),
    peerId: conversation.peer_id ?? null,
    peerBio: conversation.peer_bio ?? "",
    peerAvatar: conversation.peer_avatar ?? "",
    groupAvatar: "",
    unreadCount: conversation.unread_count ?? 0,
    lastPreview: conversation.last_msg_preview ?? "",
    lastActivityAt: conversation.last_activity_at ?? "",
  };
}

function normalizeHistoryMessage(
  message: HistoryApiMessage,
  convId: string,
  selfUserId: string | null,
): ConversationMessage {
  const isOutgoing = selfUserId === message.sender_id;

  return {
    id: message.msg_id,
    convId,
    senderId: message.sender_id,
    recipientId: message.recipient_id,
    content: decodeBase64Content(message.content_b64),
    timestampMs: message.ts_ms,
    direction: isOutgoing ? "outgoing" : "incoming",
    receiptStatus: isOutgoing ? "sent" : undefined,
  };
}

function buildOptimisticMessage(
  convId: string,
  senderId: string,
  recipientId: string,
  content: string,
): ConversationMessage {
  return {
    id: `temp-${Date.now()}-${Math.random().toString(16).slice(2, 10)}`,
    convId,
    senderId,
    recipientId,
    content,
    timestampMs: Date.now(),
    direction: "outgoing",
    pending: true,
    pendingVisible: true,
    receiptStatus: "sending",
  };
}

function isSameDeliveredMessage(
  candidate: ConversationMessage,
  incoming: ConversationMessage,
) {
  return (
    candidate.pending &&
    candidate.convId === incoming.convId &&
    candidate.senderId === incoming.senderId &&
    candidate.direction === incoming.direction &&
    candidate.content === incoming.content &&
    Math.abs(candidate.timestampMs - incoming.timestampMs) < 15_000
  );
}

function receiptRank(status: MessageReceiptStatus | undefined) {
  switch (status) {
    case "sending":
      return 0;
    case "sent":
      return 1;
    case "delivered":
      return 2;
    case "read":
      return 3;
    default:
      return -1;
  }
}

function mergeReceiptStatus(
  current: MessageReceiptStatus | undefined,
  next: MessageReceiptStatus | undefined,
) {
  return receiptRank(next) > receiptRank(current) ? next : current;
}

function isOptimisticMessage(message: ConversationMessage) {
  return message.id.startsWith("temp-");
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
        receiptStatus: mergeReceiptStatus(
          merged[sameIdIndex].receiptStatus,
          message.receiptStatus,
        ),
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
        receiptStatus: mergeReceiptStatus(
          merged[optimisticIndex].receiptStatus,
          message.receiptStatus ?? "sent",
        ),
      };
      continue;
    }

    merged.push({
      ...message,
      receiptStatus: message.receiptStatus,
    });
  }

  return merged.toSorted((left, right) => {
    if (left.timestampMs === right.timestampMs) {
      return left.id.localeCompare(right.id);
    }

    return left.timestampMs - right.timestampMs;
  });
}

function settleOptimisticMessage(
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
            pending: false,
            pendingVisible: false,
            receiptStatus: mergeReceiptStatus(message.receiptStatus, "sent"),
          }
        : message,
    ),
  };
}

function updateConversationPreview(
  conversations: ConversationSummary[],
  convId: string,
  preview: string,
  timestampMs: number,
) {
  return conversations.map((conversation) =>
    conversation.convId === convId
      ? {
          ...conversation,
          lastPreview: preview,
          lastActivityAt: new Date(timestampMs).toISOString(),
        }
      : conversation,
  );
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

function updateMessageReceiptStatus(
  messagesByConversation: Record<string, ConversationMessage[]>,
  convId: string,
  msgId: string,
  status: MessageReceiptStatus,
) {
  const current = messagesByConversation[convId];
  if (!current) {
    return messagesByConversation;
  }

  const exactIndex = current.findIndex((message) => message.id === msgId);

  if (exactIndex >= 0) {
    return {
      ...messagesByConversation,
      [convId]: current.map((message, index) =>
        index === exactIndex
          ? {
              ...message,
              pending: false,
              pendingVisible: false,
              receiptStatus: mergeReceiptStatus(message.receiptStatus, status),
            }
          : message,
      ),
    };
  }

  const optimisticIndex = current.findIndex(
    (message) => message.direction === "outgoing" && isOptimisticMessage(message),
  );

  if (optimisticIndex < 0) {
    return messagesByConversation;
  }

  return {
    ...messagesByConversation,
    [convId]: current.map((message, index) =>
      index === optimisticIndex
        ? {
            ...message,
            id: msgId,
            pending: false,
            pendingVisible: false,
            receiptStatus: mergeReceiptStatus(message.receiptStatus, status),
          }
        : message,
    ),
  };
}

function markConversationMessagesRead(
  messagesByConversation: Record<string, ConversationMessage[]>,
  convId: string,
  readAtMs: number,
): Record<string, ConversationMessage[]> {
  const current = messagesByConversation[convId];
  if (!current) {
    return messagesByConversation;
  }

  let changed = false;
  const nextMessages = current.map((message) => {
    if (
      message.direction !== "outgoing" ||
      message.pending ||
      message.timestampMs > readAtMs ||
      message.receiptStatus === "read"
    ) {
      return message;
    }

    changed = true;
    return {
      ...message,
      pending: false,
      pendingVisible: false,
      receiptStatus: "read" as MessageReceiptStatus,
    };
  });

  if (!changed) {
    return messagesByConversation;
  }

  return {
    ...messagesByConversation,
    [convId]: nextMessages,
  };
}

function removeMessage(
  messagesByConversation: Record<string, ConversationMessage[]>,
  convId: string,
  msgId: string,
) {
  const current = messagesByConversation[convId];
  if (!current) {
    return messagesByConversation;
  }

  return {
    ...messagesByConversation,
    [convId]: current.filter((message) => message.id !== msgId),
  };
}

function getMessagePreview(
  messagesByConversation: Record<string, ConversationMessage[]>,
  conversation: ConversationSummary,
) {
  const lastMessage = messagesByConversation[conversation.convId]?.at(-1);

  if (!lastMessage) {
    return {
      preview: conversation.lastPreview || "No messages yet",
      timestampMs: conversation.lastActivityAt
        ? Date.parse(conversation.lastActivityAt)
        : 0,
    };
  }

  return {
    preview: lastMessage.content,
    timestampMs: lastMessage.timestampMs,
  };
}

function extractAttachmentPath(content: string) {
  if (content.startsWith("/files/")) {
    return content;
  }

  if (content.startsWith("/api/files/")) {
    return content.slice(4);
  }

  return null;
}

function getAttachmentName(content: string) {
  const path = extractAttachmentPath(content);
  if (!path) {
    return null;
  }

  return decodeURIComponent(path.split("/").at(-1) ?? "attachment");
}

function getReceiptLabel(message: ConversationMessage) {
  switch (message.receiptStatus) {
    case "sending":
      return "Sending...";
    case "sent":
      return "Sent";
    case "delivered":
      return "Delivered";
    case "read":
      return "Read";
    default:
      return null;
  }
}

function MessageContent({
  message,
  onDownloadAttachment,
}: {
  message: ConversationMessage;
  onDownloadAttachment: (message: ConversationMessage) => void;
}) {
  const attachmentPath = extractAttachmentPath(message.content);
  const attachmentName = getAttachmentName(message.content);

  if (!attachmentPath || !attachmentName) {
    return <p className="whitespace-pre-wrap leading-6">{message.content}</p>;
  }

  return (
    <div className="space-y-3">
      <div>
        <p className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
          Attachment
        </p>
        <p className="mt-1 break-all text-sm font-medium text-[var(--foreground)]">
          {attachmentName}
        </p>
      </div>
      <button
        className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
        onClick={() => onDownloadAttachment(message)}
        type="button"
      >
        Download
      </button>
    </div>
  );
}

export default function ChatWorkspace() {
  const router = useRouter();

  const [session, setSession] = useState<StoredSession | null>(null);
  const [sessionResolved, setSessionResolved] = useState(false);
  const [socketUrl, setSocketUrl] = useState<string | null>(null);
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
  const [profilesById, setProfilesById] = useState<Record<string, UserProfile>>({});
  const [presenceByUserId, setPresenceByUserId] = useState<
    Record<string, PresenceState>
  >({});
  const [groupDetailsById, setGroupDetailsById] = useState<
    Record<string, GroupDetails>
  >({});
  const [typingByConversation, setTypingByConversation] = useState<
    Record<string, string>
  >({});

  const [threadSearch, setThreadSearch] = useState("");
  const [newChatQuery, setNewChatQuery] = useState("");
  const [newChatResults, setNewChatResults] = useState<UserSearchResult[]>([]);
  const [newChatPending, setNewChatPending] = useState(false);
  const [newChatError, setNewChatError] = useState<string | null>(null);
  const [conversationActionError, setConversationActionError] = useState<
    string | null
  >(null);
  const [creatingConversationId, setCreatingConversationId] = useState<
    string | null
  >(null);

  const [showGroupComposer, setShowGroupComposer] = useState(false);
  const [groupDraftName, setGroupDraftName] = useState("");
  const [groupDraftMembers, setGroupDraftMembers] = useState<UserSearchResult[]>([]);
  const [creatingGroup, setCreatingGroup] = useState(false);
  const [groupComposerError, setGroupComposerError] = useState<string | null>(null);

  const [memberSearchQuery, setMemberSearchQuery] = useState("");
  const [memberSearchResults, setMemberSearchResults] = useState<UserSearchResult[]>([]);
  const [memberSearchPending, setMemberSearchPending] = useState(false);
  const [memberSearchError, setMemberSearchError] = useState<string | null>(null);
  const [addingGroupMemberId, setAddingGroupMemberId] = useState<string | null>(null);
  const [removingGroupMemberId, setRemovingGroupMemberId] = useState<
    string | null
  >(null);
  const [renamingGroupName, setRenamingGroupName] = useState("");
  const [renamingGroup, setRenamingGroup] = useState(false);

  const [composerValue, setComposerValue] = useState("");
  const [sendError, setSendError] = useState<string | null>(null);
  const [pendingSend, setPendingSend] = useState(false);
  const [pendingAttachment, setPendingAttachment] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const [deletingMessageId, setDeletingMessageId] = useState<string | null>(null);
  const [downloadingMessageId, setDownloadingMessageId] = useState<string | null>(
    null,
  );

  const [detailsPanelMode, setDetailsPanelMode] = useState<DetailsPanelMode>("none");
  const [settingsBio, setSettingsBio] = useState("");
  const [settingsAvatarUrl, setSettingsAvatarUrl] = useState("");
  const [settingsNotice, setSettingsNotice] = useState<string | null>(null);
  const [settingsError, setSettingsError] = useState<string | null>(null);
  const [savingSettings, setSavingSettings] = useState(false);

  const deferredThreadSearch = useDeferredValue(threadSearch);
  const deferredNewChatQuery = useDeferredValue(newChatQuery);
  const deferredMemberSearchQuery = useDeferredValue(memberSearchQuery);

  const socketRef = useRef<WebSocket | null>(null);
  const heartbeatTimerRef = useRef<number | null>(null);
  const reconnectTimerRef = useRef<number | null>(null);
  const reconnectAttemptsRef = useRef(0);
  const preventReconnectRef = useRef(false);
  const historyByConversationRef = useRef(historyByConversation);
  const conversationsRef = useRef(conversations);
  const activeConversationIdRef = useRef(activeConversationId);
  const sessionRef = useRef<StoredSession | null>(session);
  const refreshPromiseRef = useRef<Promise<StoredSession> | null>(null);
  const typingTimersRef = useRef<Record<string, number>>({});
  const readInFlightRef = useRef<Set<string>>(new Set());
  const readBlockedUntilRef = useRef<Record<string, number>>({});
  const profileInFlightRef = useRef<Set<string>>(new Set());
  const profileBlockedUntilRef = useRef<Record<string, number>>({});
  const presenceInFlightRef = useRef<Set<string>>(new Set());
  const presenceBlockedUntilRef = useRef<Record<string, number>>({});
  const readEmittedAtRef = useRef<Record<string, number>>({});

  const selfUserId = session?.user_id ?? null;

  useEffect(() => {
    sessionRef.current = session;
  }, [session]);

  useEffect(() => {
    historyByConversationRef.current = historyByConversation;
  }, [historyByConversation]);

  useEffect(() => {
    conversationsRef.current = conversations;
  }, [conversations]);

  useEffect(() => {
    activeConversationIdRef.current = activeConversationId;
  }, [activeConversationId]);

  useEffect(() => {
    setSession(readStoredSession());
    setSessionResolved(true);
  }, []);

  const activeConversation = conversations.find(
    (conversation) => conversation.convId === activeConversationId,
  );

  const activeHistoryState = activeConversationId
    ? historyByConversation[activeConversationId] ?? createHistoryState()
    : createHistoryState();

  const activeMessages = activeConversationId
    ? messagesByConversation[activeConversationId] ?? EMPTY_MESSAGES
    : EMPTY_MESSAGES;

  const activeProfile =
    activeConversation?.kind === "dm" && activeConversation.peerId
      ? profilesById[activeConversation.peerId] ?? null
      : selfUserId
        ? profilesById[selfUserId] ?? null
        : null;

  const activePresence =
    activeConversation?.kind === "dm" && activeConversation.peerId
      ? presenceByUserId[activeConversation.peerId] ?? null
      : null;

  const activeGroupDetails =
    activeConversation?.kind === "group"
      ? groupDetailsById[activeConversation.convId] ?? null
      : null;

  const orderedConversations = useMemo(() => {
    return conversations
      .map((conversation) => {
        const preview = getMessagePreview(messagesByConversation, conversation);

        return {
          ...conversation,
          computedPreview: preview.preview,
          computedTimestampMs: preview.timestampMs,
        };
      })
      .toSorted((left, right) => right.computedTimestampMs - left.computedTimestampMs);
  }, [conversations, messagesByConversation]);

  const visibleConversations = useMemo(() => {
    const query = deferredThreadSearch.trim().toLowerCase();

    if (!query) {
      return orderedConversations;
    }

    return orderedConversations.filter((conversation) => {
      const kindLabel = conversation.kind === "group" ? "group" : "direct";
      return (
        conversation.title.toLowerCase().includes(query) ||
        conversation.computedPreview.toLowerCase().includes(query) ||
        kindLabel.includes(query)
      );
    });
  }, [deferredThreadSearch, orderedConversations]);

  const expireSession = useCallback((message?: string) => {
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
    refreshPromiseRef.current = null;
    clearStoredSession();
    sessionRef.current = null;
    setSession(null);
    setTransportNotice(message ?? "Your Loomic session expired. Please sign in again.");
    router.replace("/");
  }, [router]);

  const refreshSession = useCallback(async () => {
    const current = sessionRef.current;

    if (!current) {
      expireSession();
      throw new Error("No Loomic session is available.");
    }

    if (refreshPromiseRef.current) {
      return refreshPromiseRef.current;
    }

    const promise = (async () => {
      const response = await fetch("/api/auth/refresh", {
        method: "POST",
        headers: {
          "content-type": "application/json",
        },
        body: JSON.stringify({
          refresh_token: current.refresh_token,
        }),
      });

      const payload = (await response.json().catch(() => null)) as
        | RefreshResponse
        | { error?: string }
        | null;

      if (!response.ok) {
        expireSession("Your Loomic session expired. Please sign in again.");
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to refresh your Loomic session.",
        );
      }

      const nextSession = updateStoredSessionTokens(payload as RefreshResponse);

      if (!nextSession) {
        expireSession("Your Loomic session expired. Please sign in again.");
        throw new Error("Unable to persist refreshed Loomic credentials.");
      }

      setSession(nextSession);
      return nextSession;
    })();

    refreshPromiseRef.current = promise;

    try {
      return await promise;
    } finally {
      refreshPromiseRef.current = null;
    }
  }, [expireSession]);

  const ensureFreshSession = useCallback(async () => {
    const current = sessionRef.current;

    if (!current) {
      expireSession();
      throw new Error("No Loomic session is available.");
    }

    const exp = decodeJwtExpiry(current.access_token);
    if (!exp) {
      return current;
    }

    const expiryMs = exp * 1000;
    if (expiryMs - Date.now() <= TOKEN_REFRESH_BUFFER_MS) {
      return refreshSession();
    }

    return current;
  }, [expireSession, refreshSession]);

  const authedFetch = useCallback(
    async (
      input: string,
      init: RequestInit = {},
      options?: {
        retryOnAuthFailure?: boolean;
      },
    ) => {
      const activeSession = await ensureFreshSession();
      const headers = new Headers(init.headers);

      if (!headers.has("authorization")) {
        headers.set("authorization", `Bearer ${activeSession.access_token}`);
      }

      let response = await fetch(input, {
        ...init,
        headers,
        cache: "no-store",
      });

      const retryOnAuthFailure = options?.retryOnAuthFailure ?? true;

      if (retryOnAuthFailure && (response.status === 401 || response.status === 403)) {
        const body = await response.clone().text().catch(() => "");

        if (/token|expired|invalid/i.test(body) || response.status === 401) {
          const refreshed = await refreshSession();
          const retryHeaders = new Headers(init.headers);
          retryHeaders.set("authorization", `Bearer ${refreshed.access_token}`);

          response = await fetch(input, {
            ...init,
            headers: retryHeaders,
            cache: "no-store",
          });
        }
      }

      return response;
    },
    [ensureFreshSession, refreshSession],
  );

  const loadSelfProfile = useCallback(async () => {
    if (!selfUserId) {
      return;
    }

    try {
      const response = await authedFetch(`/api/users/${encodeURIComponent(selfUserId)}`);
      const payload = (await response.json().catch(() => null)) as
        | UserProfile
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to load your Loomic profile.",
        );
      }

      const profile = payload as UserProfile;

      setProfilesById((previous) => ({
        ...previous,
        [profile.id]: profile,
      }));
      setSettingsBio(profile.bio ?? "");
      setSettingsAvatarUrl(profile.avatar_url ?? "");
    } catch (error) {
      setSettingsError(
        error instanceof Error
          ? error.message
          : "Unable to load your Loomic profile.",
      );
    }
  }, [authedFetch, selfUserId]);

  const loadConversations = useCallback(
    async (options?: { preferredConversationId?: string; quiet?: boolean }) => {
      if (!sessionRef.current) {
        return;
      }

      const quiet = options?.quiet ?? false;

      if (!quiet) {
        setConversationsLoading(true);
      }

      setConversationsError(null);

      try {
        const response = await authedFetch("/api/conversations", {
          method: "GET",
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
          normalizeConversation,
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
              nextConversations.some((conversation) => conversation.convId === previous)
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
    [authedFetch],
  );

  const markConversationRead = useCallback(async (
    convId: string,
    options?: { force?: boolean },
  ) => {
    if (!convId) {
      return;
    }

    const now = Date.now();
    const hasUnread = conversationsRef.current.some(
      (conversation) => conversation.convId === convId && conversation.unreadCount > 0,
    );

    if (!options?.force && !hasUnread) {
      return;
    }

    const conversation = conversationsRef.current.find(
      (candidate) => candidate.convId === convId,
    );
    const socket = socketRef.current;
    const lastReadEmittedAt = readEmittedAtRef.current[convId] ?? 0;

    if (
      socket?.readyState === WebSocket.OPEN &&
      document.visibilityState === "visible" &&
      now - lastReadEmittedAt >= READ_EMIT_THROTTLE_MS
    ) {
      socket.send(
        JSON.stringify({
          type: "read",
          conv_id: convId,
          is_group: conversation?.kind === "group",
        }),
      );
      readEmittedAtRef.current[convId] = now;
    }

    setConversations((previous) => {
      let changed = false;
      const next = previous.map((conversation) => {
        if (conversation.convId !== convId || conversation.unreadCount === 0) {
          return conversation;
        }

        changed = true;
        return {
          ...conversation,
          unreadCount: 0,
        };
      });

      return changed ? next : previous;
    });

    const blockedUntil = readBlockedUntilRef.current[convId] ?? 0;
    if (readInFlightRef.current.has(convId) || blockedUntil > now) {
      return;
    }

    readInFlightRef.current.add(convId);

    try {
      const response = await authedFetch(`/api/conversations/${encodeURIComponent(convId)}/read`, {
        method: "POST",
      });

      if (!response.ok) {
        const payload = (await response.json().catch(() => null)) as
          | { error?: string }
          | null;
        throw new Error(payload?.error ?? "Unable to mark this conversation read.");
      }
    } catch {
      readBlockedUntilRef.current[convId] = Date.now() + SECONDARY_REQUEST_COOLDOWN_MS;
      // Read-state is secondary. Keep the local UI stable until the backend recovers.
    } finally {
      readInFlightRef.current.delete(convId);
    }
  }, [authedFetch]);

  const loadConversationHistory = useCallback(
    async (convId: string, mode: "replace" | "prepend" = "replace") => {
      if (!sessionRef.current || !convId) {
        return;
      }

      const currentState =
        historyByConversationRef.current[convId] ?? createHistoryState();
      const beforeCursor = mode === "prepend" ? currentState.oldestCursor : null;

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
        const response = await authedFetch(
          `/api/conversations/${encodeURIComponent(convId)}/messages?${searchParams.toString()}`,
          {
            method: "GET",
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

        if (convId === activeConversationId) {
          void markConversationRead(convId);
        }
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
    [activeConversationId, authedFetch, markConversationRead, selfUserId],
  );

  const loadProfile = useCallback(async (userId: string) => {
    if (!userId || profilesById[userId]) {
      return;
    }

    const now = Date.now();
    if (
      profileInFlightRef.current.has(userId) ||
      (profileBlockedUntilRef.current[userId] ?? 0) > now
    ) {
      return;
    }

    profileInFlightRef.current.add(userId);

    try {
      const response = await authedFetch(`/api/users/${encodeURIComponent(userId)}`);
      const payload = (await response.json().catch(() => null)) as
        | UserProfile
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to load this Loomic profile.",
        );
      }

      const profile = payload as UserProfile;

      setProfilesById((previous) => ({
        ...previous,
        [profile.id]: profile,
      }));
    } catch {
      profileBlockedUntilRef.current[userId] =
        Date.now() + SECONDARY_REQUEST_COOLDOWN_MS;
      // Keep sidebar resilient. The active conversation already has a title.
    } finally {
      profileInFlightRef.current.delete(userId);
    }
  }, [authedFetch, profilesById]);

  const loadPresence = useCallback(async (userId: string) => {
    if (!userId) {
      return;
    }

    const now = Date.now();
    if (
      presenceInFlightRef.current.has(userId) ||
      (presenceBlockedUntilRef.current[userId] ?? 0) > now
    ) {
      return;
    }

    presenceInFlightRef.current.add(userId);

    try {
      const response = await authedFetch(
        `/api/users/${encodeURIComponent(userId)}/presence`,
      );
      const payload = (await response.json().catch(() => null)) as
        | PresenceState
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to load Loomic presence.",
        );
      }

      const presence = payload as PresenceState;
      setPresenceByUserId((previous) => ({
        ...previous,
        [userId]: presence,
      }));
    } catch {
      presenceBlockedUntilRef.current[userId] =
        Date.now() + SECONDARY_REQUEST_COOLDOWN_MS;
      // Presence is secondary metadata.
    } finally {
      presenceInFlightRef.current.delete(userId);
    }
  }, [authedFetch]);

  const loadGroupDetails = useCallback(async (groupId: string) => {
    if (!groupId) {
      return;
    }

    try {
      const response = await authedFetch(`/api/groups/${encodeURIComponent(groupId)}`);
      const payload = (await response.json().catch(() => null)) as
        | GroupDetails
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to load the Loomic group.",
        );
      }

      const group = payload as GroupDetails;
      setGroupDetailsById((previous) => ({
        ...previous,
        [group.group_id]: group,
      }));
      setRenamingGroupName(group.name);
    } catch (error) {
      setConversationActionError(
        error instanceof Error ? error.message : "Unable to load the Loomic group.",
      );
    }
  }, [authedFetch]);

  const handleSocketPayload = useEffectEvent((payload: WebSocketPayload) => {
    if (payload.type === "pong") {
      setTransportNotice(null);
      return;
    }

    if (payload.type === "error") {
      const message = payload.msg ?? "The messaging service rejected this session.";
      setTransportNotice(message);

      if (/token/i.test(message)) {
        preventReconnectRef.current = true;
        refreshSession()
          .then(() => {
            preventReconnectRef.current = false;
          })
          .catch(() => undefined);
      }

      socketRef.current?.close();
      return;
    }

    if (payload.type === "delivered") {
      setMessagesByConversation((previous) =>
        updateMessageReceiptStatus(previous, payload.conv_id, payload.msg_id, "delivered"),
      );
      return;
    }

    if (payload.type === "read") {
      if (payload.user_id !== selfUserId) {
        const readAtMs =
          typeof payload.ts_ms === "number"
            ? payload.ts_ms
            : Date.now();
        setMessagesByConversation((previous) =>
          markConversationMessagesRead(previous, payload.conv_id, readAtMs),
        );
      }
      return;
    }

    if (payload.type === "typing") {
      if (payload.user_id === selfUserId) {
        return;
      }

      const conversation = conversations.find(
        (candidate) => candidate.convId === payload.conv_id,
      );
      const typingLabel =
        conversation?.kind === "group"
          ? "Someone is typing..."
          : `${conversation?.title ?? "Contact"} is typing...`;

      setTypingByConversation((previous) => ({
        ...previous,
        [payload.conv_id]: typingLabel,
      }));

      const existingTimer = typingTimersRef.current[payload.conv_id];
      if (existingTimer) {
        window.clearTimeout(existingTimer);
      }

      typingTimersRef.current[payload.conv_id] = window.setTimeout(() => {
        setTypingByConversation((previous) => {
          const next = { ...previous };
          delete next[payload.conv_id];
          return next;
        });
      }, 3_000);

      return;
    }

    if (payload.type === "delete_notify") {
      setMessagesByConversation((previous) =>
        removeMessage(previous, payload.conv_id, payload.msg_id),
      );
      return;
    }

    const convId = payload.group_id ?? payload.conv_id;
    const isOutgoing = payload.sender_id === selfUserId;

    const nextMessage: ConversationMessage = {
      id: payload.msg_id,
      convId,
      senderId: payload.sender_id,
      recipientId: convId,
      content: payload.content,
      timestampMs: payload.ts_ms,
      direction: isOutgoing ? "outgoing" : "incoming",
      receiptStatus: isOutgoing ? "sent" : undefined,
    };

    setMessagesByConversation((previous) => ({
      ...previous,
      [convId]: mergeConversationMessages(previous[convId] ?? EMPTY_MESSAGES, [nextMessage]),
    }));

    setConversations((previous) =>
      updateConversationPreview(previous, convId, nextMessage.content, nextMessage.timestampMs),
    );

    if (!isOutgoing) {
      setConversations((previous) =>
        previous.map((conversation) => {
          if (conversation.convId !== convId) {
            return conversation;
          }

          const unreadCount =
            activeConversationId === convId && document.visibilityState === "visible"
              ? 0
              : conversation.unreadCount + 1;

          return unreadCount === conversation.unreadCount
            ? conversation
            : {
                ...conversation,
                unreadCount,
              };
        }),
      );

      if (activeConversationId === convId && document.visibilityState === "visible") {
        void markConversationRead(convId, { force: true });
      }
    }

    if (payload.sender_id !== selfUserId && activeConversationId !== convId) {
      void loadConversations({
        preferredConversationId: activeConversationId || convId,
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

    void loadSelfProfile();
  }, [loadSelfProfile, session, sessionResolved]);

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

    void fetchSocketConfig();

    return () => {
      disposed = true;
    };
  }, [session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    void loadConversations();
  }, [loadConversations, session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    const interval = window.setInterval(() => {
      void ensureFreshSession().catch(() => undefined);
    }, 60_000);

    return () => {
      window.clearInterval(interval);
    };
  }, [ensureFreshSession, session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    const query = deferredNewChatQuery.trim();

    if (query.length < 2) {
      setNewChatResults([]);
      setNewChatError(null);
      setNewChatPending(false);
      return;
    }

    const controller = new AbortController();
    setNewChatPending(true);
    setNewChatError(null);

    authedFetch(`/api/users/search?q=${encodeURIComponent(query)}&limit=8`, {
      method: "GET",
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
        setNewChatResults(users.filter((user) => user.id !== selfUserId));
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) {
          return;
        }

        setNewChatError(
          error instanceof Error ? error.message : "Unable to search Loomic users.",
        );
      })
      .finally(() => {
        if (!controller.signal.aborted) {
          setNewChatPending(false);
        }
      });

    return () => {
      controller.abort();
    };
  }, [authedFetch, deferredNewChatQuery, selfUserId, session, sessionResolved]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    if (!showGroupComposer && detailsPanelMode !== "group") {
      setMemberSearchResults([]);
      setMemberSearchError(null);
      setMemberSearchPending(false);
      return;
    }

    const query = deferredMemberSearchQuery.trim();
    if (query.length < 2) {
      setMemberSearchResults([]);
      setMemberSearchError(null);
      setMemberSearchPending(false);
      return;
    }

    const controller = new AbortController();
    setMemberSearchPending(true);
    setMemberSearchError(null);

    authedFetch(`/api/users/search?q=${encodeURIComponent(query)}&limit=8`, {
      method: "GET",
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
        const excludedIds = new Set<string>([
          selfUserId ?? "",
          ...groupDraftMembers.map((member) => member.id),
          ...(activeGroupDetails?.members.map((member) => member.user_id) ?? []),
        ]);
        setMemberSearchResults(users.filter((user) => !excludedIds.has(user.id)));
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) {
          return;
        }

        setMemberSearchError(
          error instanceof Error ? error.message : "Unable to search Loomic users.",
        );
      })
      .finally(() => {
        if (!controller.signal.aborted) {
          setMemberSearchPending(false);
        }
      });

    return () => {
      controller.abort();
    };
  }, [
    activeGroupDetails,
    authedFetch,
    deferredMemberSearchQuery,
    detailsPanelMode,
    groupDraftMembers,
    selfUserId,
    session,
    sessionResolved,
    showGroupComposer,
  ]);

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
      setTransportNotice(detail);

      reconnectTimerRef.current = window.setTimeout(() => {
        reconnectTimerRef.current = null;
        openSocket();
      }, waitMs);
    };

    const openSocket = () => {
      if (disposed) {
        return;
      }

      const nextSocket = new WebSocket(socketUrl);
      socketRef.current = nextSocket;

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

        ensureFreshSession()
          .then((currentSession) => {
            nextSocket.send(
              JSON.stringify({
                type: "auth",
                token: currentSession.access_token,
              }),
            );

            const activeConvId = activeConversationIdRef.current;
            if (activeConvId && document.visibilityState === "visible") {
              void markConversationRead(activeConvId, { force: true });
            }

            heartbeatTimerRef.current = window.setInterval(() => {
              if (nextSocket.readyState === WebSocket.OPEN) {
                nextSocket.send(JSON.stringify({ type: "ping" }));
              }
            }, HEARTBEAT_INTERVAL_MS);
          })
          .catch((error) => {
            setTransportNotice(
              error instanceof Error
                ? error.message
                : "Unable to authenticate the Loomic socket.",
            );
            nextSocket.close();
          });
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
  }, [
    ensureFreshSession,
    markConversationRead,
    session,
    sessionResolved,
    socketUrl,
  ]);

  useEffect(() => {
    if (!sessionResolved || !session || !activeConversationId) {
      return;
    }

    const historyState =
      historyByConversationRef.current[activeConversationId] ?? createHistoryState();

    if (!historyState.loaded && !historyState.loading) {
      void loadConversationHistory(activeConversationId, "replace");
    } else {
      void markConversationRead(activeConversationId);
    }

    if (activeConversation?.kind === "dm" && activeConversation.peerId) {
      void loadProfile(activeConversation.peerId);
      void loadPresence(activeConversation.peerId);
    }

    if (activeConversation?.kind === "group") {
      void loadGroupDetails(activeConversation.convId);
    }
  }, [
    activeConversation,
    activeConversationId,
    loadConversationHistory,
    loadGroupDetails,
    loadPresence,
    loadProfile,
    markConversationRead,
    session,
    sessionResolved,
  ]);

  useEffect(() => {
    if (!sessionResolved || !session) {
      return;
    }

    const markActiveVisibleConversationRead = () => {
      const convId = activeConversationIdRef.current;
      if (!convId || document.visibilityState !== "visible") {
        return;
      }

      void markConversationRead(convId, { force: true });
    };

    document.addEventListener("visibilitychange", markActiveVisibleConversationRead);
    window.addEventListener("focus", markActiveVisibleConversationRead);

    return () => {
      document.removeEventListener(
        "visibilitychange",
        markActiveVisibleConversationRead,
      );
      window.removeEventListener("focus", markActiveVisibleConversationRead);
    };
  }, [markConversationRead, session, sessionResolved]);

  async function handleCreateConversation(user: UserSearchResult) {
    setConversationActionError(null);

    const existingConversation = conversations.find(
      (conversation) => conversation.kind === "dm" && conversation.peerId === user.id,
    );

    if (existingConversation) {
      startTransition(() => {
        setActiveConversationId(existingConversation.convId);
        setNewChatQuery("");
        setNewChatResults([]);
      });
      return;
    }

    setCreatingConversationId(user.id);

    try {
      const response = await authedFetch("/api/conversations", {
        method: "POST",
        headers: {
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
            kind: "dm",
            title: user.username,
            peerId: user.id,
            peerBio: "",
            peerAvatar: "",
            groupAvatar: "",
            unreadCount: 0,
            lastPreview: "",
            lastActivityAt: (payload as CreateConversationResponse).created_at,
          }),
        );
        setActiveConversationId(convId);
        setNewChatQuery("");
        setNewChatResults([]);
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

  async function handleCreateGroup() {
    setGroupComposerError(null);

    if (!groupDraftName.trim()) {
      setGroupComposerError("Choose a name for the new group.");
      return;
    }

    if (groupDraftMembers.length === 0) {
      setGroupComposerError("Add at least one member to the new group.");
      return;
    }

    setCreatingGroup(true);

    try {
      const response = await authedFetch("/api/groups", {
        method: "POST",
        headers: {
          "content-type": "application/json",
        },
        body: JSON.stringify({
          name: groupDraftName.trim(),
          member_ids: groupDraftMembers.map((member) => member.id),
        }),
      });

      const payload = (await response.json().catch(() => null)) as
        | CreateGroupResponse
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to create the Loomic group.",
        );
      }

      const created = payload as CreateGroupResponse;

      setConversations((previous) =>
        ensureConversationSummary(previous, {
          convId: created.group_id,
          kind: "group",
          title: created.name,
          peerId: null,
          peerBio: "",
          peerAvatar: "",
          groupAvatar: "",
          unreadCount: 0,
          lastPreview: "",
          lastActivityAt: created.created_at,
        }),
      );

      setGroupDraftName("");
      setGroupDraftMembers([]);
      setShowGroupComposer(false);
      setMemberSearchQuery("");
      setActiveConversationId(created.group_id);
      setDetailsPanelMode("group");

      void loadGroupDetails(created.group_id);
    } catch (error) {
      setGroupComposerError(
        error instanceof Error ? error.message : "Unable to create the Loomic group.",
      );
    } finally {
      setCreatingGroup(false);
    }
  }

  async function handleLoadOlderMessages() {
    if (!activeConversationId) {
      return;
    }

    await loadConversationHistory(activeConversationId, "prepend");
  }

  function handleSelectConversation(convId: string) {
    startTransition(() => {
      setActiveConversationId(convId);
    });

    void markConversationRead(convId);
  }

  async function handleSend(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setSendError(null);

    if (!session || !activeConversation) {
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
      activeConversation.convId,
      selfUserId ?? "0",
      activeConversation.kind === "dm"
        ? activeConversation.peerId ?? activeConversation.convId
        : activeConversation.convId,
      content,
    );

    setPendingSend(true);

    try {
      socket.send(
        JSON.stringify(
          activeConversation.kind === "group"
            ? {
                type: "group_chat",
                group_id: activeConversation.convId,
                content,
              }
            : {
                type: "chat",
                conv_id: activeConversation.convId,
                content,
              },
        ),
      );

      setMessagesByConversation((previous) => ({
        ...previous,
        [activeConversation.convId]: mergeConversationMessages(
          previous[activeConversation.convId] ?? EMPTY_MESSAGES,
          [optimisticMessage],
        ),
      }));

      setConversations((previous) =>
        updateConversationPreview(
          previous,
          activeConversation.convId,
          content,
          optimisticMessage.timestampMs,
        ),
      );

      window.setTimeout(() => {
        setMessagesByConversation((previous) =>
          settleOptimisticMessage(previous, activeConversation.convId, optimisticMessage.id),
        );
      }, 1_200);

      setComposerValue("");
    } catch (error) {
      setSendError(
        error instanceof Error ? error.message : "Unable to send the Loomic message.",
      );
    } finally {
      setPendingSend(false);
    }
  }

  async function handleDeleteMessage(message: ConversationMessage) {
    if (!message || deletingMessageId) {
      return;
    }

    setDeleteError(null);
    setDeletingMessageId(message.id);

    try {
      const response = await authedFetch(
        `/api/messages/${encodeURIComponent(message.id)}?conv_id=${encodeURIComponent(message.convId)}`,
        {
          method: "DELETE",
        },
      );

      if (!response.ok) {
        const payload = (await response.json().catch(() => null)) as
          | { error?: string }
          | null;
        throw new Error(
          payload?.error ?? "Unable to delete the Loomic message.",
        );
      }

      setMessagesByConversation((previous) =>
        removeMessage(previous, message.convId, message.id),
      );
    } catch (error) {
      setDeleteError(
        error instanceof Error ? error.message : "Unable to delete the Loomic message.",
      );
    } finally {
      setDeletingMessageId(null);
    }
  }

  async function handlePickAttachment(file: File) {
    if (!file || !activeConversation) {
      return;
    }

    setSendError(null);
    setPendingAttachment(true);

    try {
      const formData = new FormData();
      formData.append("file", file, file.name);

      const response = await authedFetch("/api/upload", {
        method: "POST",
        body: formData,
      });

      const payload = (await response.json().catch(() => null)) as
        | UploadResponse
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          (payload as { error?: string } | null)?.error ??
            "Unable to upload the Loomic file.",
        );
      }

      setComposerValue((payload as UploadResponse).url);
    } catch (error) {
      setSendError(
        error instanceof Error ? error.message : "Unable to upload the Loomic file.",
      );
    } finally {
      setPendingAttachment(false);
    }
  }

  async function handleDownloadAttachment(message: ConversationMessage) {
    const path = extractAttachmentPath(message.content);
    const name = getAttachmentName(message.content);

    if (!path || !name) {
      return;
    }

    setDownloadingMessageId(message.id);
    setSendError(null);

    try {
      const response = await authedFetch(`/api${path}`, {
        method: "GET",
      });

      if (!response.ok) {
        const payload = await response.text().catch(() => "");
        throw new Error(payload || "Unable to download the Loomic attachment.");
      }

      const blob = await response.blob();
      const objectUrl = URL.createObjectURL(blob);
      const anchor = document.createElement("a");
      anchor.href = objectUrl;
      anchor.download = name;
      document.body.append(anchor);
      anchor.click();
      anchor.remove();
      URL.revokeObjectURL(objectUrl);
    } catch (error) {
      setSendError(
        error instanceof Error
          ? error.message
          : "Unable to download the Loomic attachment.",
      );
    } finally {
      setDownloadingMessageId(null);
    }
  }

  async function handleSaveProfile(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();

    if (!selfUserId) {
      return;
    }

    setSavingSettings(true);
    setSettingsError(null);
    setSettingsNotice(null);

    try {
      const response = await authedFetch(`/api/users/${encodeURIComponent(selfUserId)}`, {
        method: "PATCH",
        headers: {
          "content-type": "application/json",
        },
        body: JSON.stringify({
          bio: settingsBio,
          avatar_url: settingsAvatarUrl,
        }),
      });

      const payload = (await response.json().catch(() => null)) as
        | { updated?: boolean; error?: string }
        | null;

      if (!response.ok) {
        throw new Error(
          payload?.error ?? "Unable to update your Loomic profile.",
        );
      }

      const updatedProfile: UserProfile = {
        id: selfUserId,
        username: session?.username ?? activeProfile?.username ?? "You",
        bio: settingsBio,
        avatar_url: settingsAvatarUrl,
      };

      setProfilesById((previous) => ({
        ...previous,
        [selfUserId]: {
          ...(previous[selfUserId] ?? updatedProfile),
          ...updatedProfile,
        },
      }));
      setSettingsNotice("Profile saved.");
    } catch (error) {
      setSettingsError(
        error instanceof Error ? error.message : "Unable to update your Loomic profile.",
      );
    } finally {
      setSavingSettings(false);
    }
  }

  async function handleRenameGroup(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();

    if (!activeConversation || activeConversation.kind !== "group") {
      return;
    }

    if (!renamingGroupName.trim()) {
      setConversationActionError("Choose a name for the Loomic group.");
      return;
    }

    setRenamingGroup(true);
    setConversationActionError(null);

    try {
      const response = await authedFetch(
        `/api/groups/${encodeURIComponent(activeConversation.convId)}`,
        {
          method: "PATCH",
          headers: {
            "content-type": "application/json",
          },
          body: JSON.stringify({
            name: renamingGroupName.trim(),
          }),
        },
      );

      const payload = (await response.json().catch(() => null)) as
        | GroupRenameResponse
        | { error?: string }
        | null;

      if (!response.ok) {
        const errorPayload = payload as { error?: string } | null;
        throw new Error(
          errorPayload?.error ?? "Unable to rename the Loomic group.",
        );
      }

      setConversations((previous) =>
        previous.map((conversation) =>
          conversation.convId === activeConversation.convId
            ? {
                ...conversation,
                title: (payload as GroupRenameResponse).name,
              }
            : conversation,
        ),
      );

      setGroupDetailsById((previous) => ({
        ...previous,
        [activeConversation.convId]: previous[activeConversation.convId]
          ? {
              ...previous[activeConversation.convId],
              name: (payload as GroupRenameResponse).name,
            }
          : previous[activeConversation.convId],
      }));
    } catch (error) {
      setConversationActionError(
        error instanceof Error ? error.message : "Unable to rename the Loomic group.",
      );
    } finally {
      setRenamingGroup(false);
    }
  }

  async function handleAddGroupMember(user: UserSearchResult) {
    if (!activeConversation || activeConversation.kind !== "group") {
      return;
    }

    setAddingGroupMemberId(user.id);
    setConversationActionError(null);

    try {
      const response = await authedFetch(
        `/api/groups/${encodeURIComponent(activeConversation.convId)}/members`,
        {
          method: "POST",
          headers: {
            "content-type": "application/json",
          },
          body: JSON.stringify({
            user_id: user.id,
          }),
        },
      );

      const payload = (await response.json().catch(() => null)) as
        | GroupMemberMutationResponse
        | { error?: string }
        | null;

      if (!response.ok) {
        const errorPayload = payload as { error?: string } | null;
        throw new Error(
          errorPayload?.error ?? "Unable to add the group member.",
        );
      }

      setMemberSearchQuery("");
      await loadGroupDetails(activeConversation.convId);
    } catch (error) {
      setConversationActionError(
        error instanceof Error ? error.message : "Unable to add the group member.",
      );
    } finally {
      setAddingGroupMemberId(null);
    }
  }

  async function handleRemoveGroupMember(member: GroupMember) {
    if (!activeConversation || activeConversation.kind !== "group") {
      return;
    }

    setRemovingGroupMemberId(member.user_id);
    setConversationActionError(null);

    try {
      const response = await authedFetch(
        `/api/groups/${encodeURIComponent(activeConversation.convId)}/members/${encodeURIComponent(member.user_id)}`,
        {
          method: "DELETE",
        },
      );

      if (!response.ok) {
        const payload = (await response.json().catch(() => null)) as
          | { error?: string }
          | null;
        throw new Error(payload?.error ?? "Unable to remove the group member.");
      }

      if (member.user_id === selfUserId) {
        setConversations((previous) =>
          previous.filter((conversation) => conversation.convId !== activeConversation.convId),
        );
        setDetailsPanelMode("none");
        setActiveConversationId("");
      } else {
        await loadGroupDetails(activeConversation.convId);
      }
    } catch (error) {
      setConversationActionError(
        error instanceof Error ? error.message : "Unable to remove the group member.",
      );
    } finally {
      setRemovingGroupMemberId(null);
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
          authorization: `Bearer ${session.access_token}`,
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

  const activeTypingNotice =
    activeConversationId ? typingByConversation[activeConversationId] ?? null : null;

  const canManageActiveGroup =
    activeConversation?.kind === "group" &&
    activeGroupDetails?.members.some(
      (member) => member.user_id === selfUserId && member.role === "admin",
    );

  const showSidebarSearchResults =
    newChatPending ||
    Boolean(newChatError) ||
    Boolean(conversationActionError) ||
    deferredNewChatQuery.trim().length >= 2;

  return (
    <section className="section-fade flex h-full min-h-0 flex-col overflow-hidden">
      <div className="surface-panel grid h-full min-h-0 overflow-hidden sm:rounded-[28px] lg:grid-cols-[330px_minmax(0,1fr)]">
        <aside className="flex min-h-0 flex-col overflow-hidden border-b border-[var(--line)] bg-[rgba(6,12,18,0.62)] lg:border-b-0 lg:border-r">
          <div className="shrink-0 border-b border-[var(--line)] px-4 py-4">
            <div className="flex items-center justify-between gap-3">
              <div className="min-w-0">
                <p className="truncate text-base font-semibold text-[var(--foreground)]">Loomic</p>
                <p className="truncate text-sm text-[var(--muted)]">{session.username}</p>
              </div>
              <div className="flex items-center gap-2">
                <button
                  className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                  onClick={() => setDetailsPanelMode("settings")}
                  type="button"
                >
                  Settings
                </button>
                <button
                  className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                  onClick={() => {
                    void handleSignOut();
                  }}
                  type="button"
                >
                  Sign out
                </button>
              </div>
            </div>

            <div className="mt-4 flex items-center gap-2">
              <input
                className="w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                onChange={(event) => setNewChatQuery(event.target.value)}
                placeholder="Start a direct message"
                value={newChatQuery}
              />
              <button
                className={`rounded-2xl border px-3 py-3 text-sm ${
                  showGroupComposer
                    ? "border-[rgba(114,214,201,0.42)] bg-[rgba(114,214,201,0.14)] text-[var(--foreground)]"
                    : "border-[var(--line)] bg-[var(--panel)] text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                }`}
                onClick={() => {
                  setShowGroupComposer((previous) => !previous);
                  setMemberSearchQuery("");
                  setGroupComposerError(null);
                }}
                type="button"
              >
                Group
              </button>
            </div>

            {showGroupComposer ? (
              <div className="mt-4 rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-4">
                <div className="flex items-center justify-between gap-3">
                  <div>
                    <p className="text-sm font-medium text-[var(--foreground)]">New group</p>
                    <p className="text-xs text-[var(--muted)]">
                      Pick members, name the room, and create it.
                    </p>
                  </div>
                  <button
                    className="text-xs text-[var(--muted)] hover:text-[var(--foreground)]"
                    onClick={() => {
                      setShowGroupComposer(false);
                      setGroupDraftName("");
                      setGroupDraftMembers([]);
                      setMemberSearchQuery("");
                      setGroupComposerError(null);
                    }}
                    type="button"
                  >
                    Close
                  </button>
                </div>

                <input
                  className="mt-4 w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                  onChange={(event) => setGroupDraftName(event.target.value)}
                  placeholder="Group name"
                  value={groupDraftName}
                />

                <input
                  className="mt-3 w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                  onChange={(event) => setMemberSearchQuery(event.target.value)}
                  placeholder="Search users to add"
                  value={memberSearchQuery}
                />

                {groupDraftMembers.length > 0 ? (
                  <div className="mt-3 flex flex-wrap gap-2">
                    {groupDraftMembers.map((member) => (
                      <button
                        key={member.id}
                        className="rounded-full border border-[var(--line)] bg-[var(--panel)] px-3 py-1.5 text-xs text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                        onClick={() =>
                          setGroupDraftMembers((previous) =>
                            previous.filter((candidate) => candidate.id !== member.id),
                          )
                        }
                        type="button"
                      >
                        {member.username} ×
                      </button>
                    ))}
                  </div>
                ) : null}

                {deferredMemberSearchQuery.trim().length >= 2 ? (
                  <div className="token-log mt-3 max-h-40 space-y-2 overflow-y-auto pr-1">
                    {memberSearchPending ? (
                      <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">
                        Searching...
                      </div>
                    ) : null}
                    {!memberSearchPending && memberSearchError ? (
                      <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                        {memberSearchError}
                      </div>
                    ) : null}
                    {!memberSearchPending &&
                    !memberSearchError &&
                    memberSearchResults.length === 0 ? (
                      <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">
                        No matches
                      </div>
                    ) : null}
                    {memberSearchResults.map((user) => (
                      <div
                        key={user.id}
                        className="flex items-center justify-between gap-3 rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3"
                      >
                        <div className="min-w-0">
                          <p className="truncate text-sm text-[var(--foreground)]">
                            {user.username}
                          </p>
                        </div>
                        <button
                          className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                          onClick={() => {
                            setGroupDraftMembers((previous) =>
                              previous.some((candidate) => candidate.id === user.id)
                                ? previous
                                : [...previous, user],
                            );
                          }}
                          type="button"
                        >
                          Add
                        </button>
                      </div>
                    ))}
                  </div>
                ) : null}

                {groupComposerError ? (
                  <div className="mt-3 rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                    {groupComposerError}
                  </div>
                ) : null}

                <button
                  className="mt-4 inline-flex min-h-11 w-full items-center justify-center rounded-2xl bg-[rgba(114,214,201,0.12)] px-4 text-sm font-medium text-[var(--foreground)] hover:bg-[rgba(114,214,201,0.2)] disabled:cursor-not-allowed disabled:opacity-60"
                  disabled={creatingGroup}
                  onClick={() => {
                    void handleCreateGroup();
                  }}
                  type="button"
                >
                  {creatingGroup ? "Creating..." : "Create group"}
                </button>
              </div>
            ) : null}

            {showSidebarSearchResults ? (
              <div className="token-log mt-3 max-h-52 space-y-2 overflow-y-auto">
                {newChatPending ? (
                  <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">
                    Searching...
                  </div>
                ) : null}
                {!newChatPending && newChatError ? (
                  <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                    {newChatError}
                  </div>
                ) : null}
                {!newChatPending &&
                !newChatError &&
                newChatResults.length === 0 ? (
                  <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">
                    No matches
                  </div>
                ) : null}
                {newChatResults.map((user) => (
                  <div
                    key={user.id}
                    className="flex items-center justify-between gap-3 rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3"
                  >
                    <div className="flex min-w-0 items-center gap-3">
                      <Avatar
                        alt={`${user.username} avatar`}
                        className="h-9 w-9 text-xs font-semibold"
                        name={user.username}
                        src={user.avatar_url}
                      />
                      <p className="truncate text-sm text-[var(--foreground)]">
                        {user.username}
                      </p>
                    </div>
                    <button
                      className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)] disabled:cursor-not-allowed disabled:opacity-60"
                      disabled={creatingConversationId === user.id}
                      onClick={() => {
                        void handleCreateConversation(user);
                      }}
                      type="button"
                    >
                      {creatingConversationId === user.id ? "..." : "Open"}
                    </button>
                  </div>
                ))}
                {conversationActionError ? (
                  <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                    {conversationActionError}
                  </div>
                ) : null}
              </div>
            ) : null}
          </div>

          <div className="shrink-0 border-b border-[var(--line)] px-4 py-3">
            <div className="flex items-center gap-2">
              <input
                className="w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                onChange={(event) => setThreadSearch(event.target.value)}
                placeholder="Search conversations"
                value={threadSearch}
              />
              <button
                className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                onClick={() => {
                  void loadConversations();
                }}
                type="button"
              >
                Refresh
              </button>
            </div>
          </div>

          {conversationsError ? (
            <div className="px-4 pt-3">
              <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                {conversationsError}
              </div>
            </div>
          ) : null}

          <div className="min-h-0 flex-1 overflow-hidden p-2">
            <div className="token-log h-full overflow-y-auto pr-1">
              {conversationsLoading && conversations.length === 0 ? (
                <div className="rounded-2xl px-3 py-3 text-sm text-[var(--muted)]">
                  Loading...
                </div>
              ) : null}
              {!conversationsLoading && visibleConversations.length === 0 ? (
                <div className="rounded-2xl px-3 py-3 text-sm text-[var(--muted)]">
                  No conversations
                </div>
              ) : null}
              {visibleConversations.map((conversation) => (
                <button
                  key={conversation.convId}
                  className={`mb-1.5 flex w-full items-center gap-3 rounded-2xl px-3 py-3 text-left ${
                    conversation.convId === activeConversationId
                      ? "bg-[var(--panel)] shadow-sm ring-1 ring-[var(--line)]"
                      : "hover:bg-[rgba(255,255,255,0.04)]"
                  }`}
                  onClick={() => handleSelectConversation(conversation.convId)}
                  type="button"
                >
                  <Avatar
                    alt={`${conversation.title} avatar`}
                    className="h-10 w-10 text-xs font-semibold"
                    name={conversation.title}
                    src={
                      conversation.kind === "group"
                        ? conversation.groupAvatar
                        : conversation.peerAvatar
                    }
                  />
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center justify-between gap-3">
                      <div className="min-w-0">
                        <p className="truncate text-sm font-medium text-[var(--foreground)]">
                          {conversation.title}
                        </p>
                        <p className="mt-0.5 text-[11px] uppercase tracking-[0.18em] text-[var(--muted)]">
                          {conversation.kind === "group" ? "Group" : "Direct"}
                        </p>
                      </div>
                      <div className="shrink-0 text-right">
                        <span className="block text-xs text-[var(--muted)]">
                          {conversation.computedTimestampMs > 0
                            ? formatRelativeTime(conversation.computedTimestampMs)
                            : formatIsoDate(conversation.lastActivityAt)}
                        </span>
                        {conversation.unreadCount > 0 ? (
                          <span className="mt-1 inline-flex min-w-6 items-center justify-center rounded-full bg-[rgba(114,214,201,0.16)] px-2 py-0.5 text-[11px] font-semibold text-[var(--signal)]">
                            {conversation.unreadCount}
                          </span>
                        ) : null}
                      </div>
                    </div>
                    <p className="mt-1 truncate text-sm text-[var(--muted)]">
                      {conversation.computedPreview || "No messages yet"}
                    </p>
                  </div>
                </button>
              ))}
            </div>
          </div>
        </aside>

        <div className="flex min-h-0 flex-col overflow-hidden bg-[rgba(8,14,20,0.58)]">
          <header className="shrink-0 border-b border-[var(--line)] px-4 py-4 sm:px-6">
            <div className="flex items-center justify-between gap-4">
              <div className="flex min-w-0 items-center gap-3">
                <Avatar
                  alt={`${activeConversation?.title ?? "Messages"} avatar`}
                  className="h-10 w-10 text-xs font-semibold"
                  name={activeConversation?.title ?? "Messages"}
                  src={
                    activeConversation?.kind === "group"
                      ? activeConversation.groupAvatar
                      : activeConversation?.peerAvatar
                  }
                />
                <div className="min-w-0">
                  <h2 className="truncate text-sm font-semibold text-[var(--foreground)] sm:text-base">
                    {activeConversation?.title ?? "Messages"}
                  </h2>
                  <p className="truncate text-xs text-[var(--muted)]">
                    {activeTypingNotice
                      ? activeTypingNotice
                      : activeConversation?.kind === "group"
                        ? `${activeGroupDetails?.members.length ?? 0} members`
                        : formatPresence(activePresence)}
                  </p>
                </div>
              </div>

              <div className="flex items-center gap-2">
                {activeConversation?.kind === "group" ? (
                  <button
                    className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                    onClick={() => setDetailsPanelMode("group")}
                    type="button"
                  >
                    Group details
                  </button>
                ) : activeConversation?.kind === "dm" ? (
                  <button
                    className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                    onClick={() => setDetailsPanelMode("profile")}
                    type="button"
                  >
                    Profile
                  </button>
                ) : null}
                {activeConversation && activeHistoryState.hasMore ? (
                  <button
                    className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)] disabled:cursor-not-allowed disabled:opacity-60"
                    disabled={activeHistoryState.loadingMore}
                    onClick={() => {
                      void handleLoadOlderMessages();
                    }}
                    type="button"
                  >
                    {activeHistoryState.loadingMore ? "Loading..." : "Load older"}
                  </button>
                ) : null}
              </div>
            </div>
          </header>

          {transportNotice ? (
            <div className="shrink-0 border-b border-[rgba(245,143,124,0.18)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)] sm:px-6">
              {transportNotice}
            </div>
          ) : null}
          {activeHistoryState.error ? (
            <div className="shrink-0 border-b border-[rgba(245,143,124,0.18)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)] sm:px-6">
              {activeHistoryState.error}
            </div>
          ) : null}
          {conversationActionError ? (
            <div className="shrink-0 border-b border-[rgba(245,143,124,0.18)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)] sm:px-6">
              {conversationActionError}
            </div>
          ) : null}

          <div className="min-h-0 flex-1 overflow-hidden">
            <div className="token-log h-full overflow-y-auto px-4 py-4 sm:px-6">
              <div className="mx-auto flex min-h-full w-full max-w-4xl flex-col gap-3">
                {activeMessages.length === 0 &&
                  (activeConversation && activeHistoryState.loading ? (
                    <div className="flex h-full min-h-[240px] items-center justify-center text-sm text-[var(--muted)]">
                      Loading...
                    </div>
                  ) : !activeConversation ? (
                    <div className="flex h-full min-h-[240px] items-center justify-center rounded-[28px] border border-dashed border-[var(--line)] bg-[var(--panel-subtle)] px-6 text-center text-sm text-[var(--muted)]">
                      Select a conversation
                    </div>
                  ) : (
                    <div className="flex h-full min-h-[240px] items-center justify-center rounded-[28px] border border-dashed border-[var(--line)] bg-[var(--panel-subtle)] px-6 text-center text-sm text-[var(--muted)]">
                      No messages yet
                    </div>
                  ))}

                {activeMessages.map((message) => {
                  const isOutgoing = message.direction === "outgoing";
                  const receiptLabel = getReceiptLabel(message);

                  return (
                    <div
                      key={message.id}
                      className={`flex ${isOutgoing ? "justify-end" : "justify-start"}`}
                    >
                      <article
                        className={`max-w-[88%] rounded-[22px] px-4 py-3 text-sm sm:max-w-[78%] ${
                          isOutgoing
                            ? "border border-[rgba(241,205,146,0.22)] bg-[rgba(241,205,146,0.14)] text-[var(--foreground)]"
                            : "border border-[var(--line)] bg-[var(--panel-subtle)] text-[var(--foreground)]"
                        }`}
                      >
                        <MessageContent
                          message={message}
                          onDownloadAttachment={handleDownloadAttachment}
                        />
                        <div
                          className={`mt-2 flex items-center justify-between gap-4 text-[11px] ${
                            isOutgoing ? "text-[rgba(245,236,220,0.62)]" : "text-[var(--muted)]"
                          }`}
                        >
                          <span>{formatClock(message.timestampMs)}</span>
                          <div className="flex items-center gap-3">
                            {isOutgoing && receiptLabel ? <span>{receiptLabel}</span> : null}
                            {isOutgoing ? (
                              <button
                                className="text-[11px] text-inherit hover:text-[var(--foreground)] disabled:cursor-not-allowed disabled:opacity-60"
                                disabled={deletingMessageId === message.id}
                                onClick={() => {
                                  void handleDeleteMessage(message);
                                }}
                                type="button"
                              >
                                {deletingMessageId === message.id ? "Deleting..." : "Delete"}
                              </button>
                            ) : null}
                            {extractAttachmentPath(message.content) ? (
                              <span>
                                {downloadingMessageId === message.id ? "Downloading..." : ""}
                              </span>
                            ) : null}
                          </div>
                        </div>
                      </article>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>

          <footer className="shrink-0 border-t border-[var(--line)] px-4 py-4 sm:px-6">
            {sendError ? (
              <div className="mb-3 rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                {sendError}
              </div>
            ) : null}
            {deleteError ? (
              <div className="mb-3 rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                {deleteError}
              </div>
            ) : null}

            <form onSubmit={handleSend}>
              <div className="flex items-end gap-3 rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-3">
                <label className="inline-flex min-h-11 shrink-0 items-center justify-center rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]">
                  <input
                    className="hidden"
                    onChange={(event) => {
                      const file = event.target.files?.[0];
                      if (file) {
                        void handlePickAttachment(file);
                      }
                      event.currentTarget.value = "";
                    }}
                    type="file"
                  />
                  {pendingAttachment ? "Uploading..." : "Attach"}
                </label>
                <textarea
                  className="min-h-11 flex-1 resize-none bg-transparent px-1 py-2 text-sm leading-6 text-[var(--foreground)] outline-none"
                  onChange={(event) => setComposerValue(event.target.value)}
                  onKeyDown={(event) => {
                    if (
                      event.key !== "Enter" ||
                      event.shiftKey ||
                      event.ctrlKey ||
                      event.altKey ||
                      event.metaKey ||
                      event.nativeEvent.isComposing
                    ) {
                      return;
                    }

                    event.preventDefault();
                    event.currentTarget.form?.requestSubmit();
                  }}
                  placeholder={
                    activeConversation
                      ? `Message ${activeConversation.title}`
                      : "Choose a conversation"
                  }
                  value={composerValue}
                />
                <button
                  className="inline-flex min-h-11 items-center justify-center rounded-2xl bg-[rgba(114,214,201,0.12)] px-4 text-sm font-medium text-[var(--foreground)] hover:bg-[rgba(114,214,201,0.2)] disabled:cursor-not-allowed disabled:opacity-50"
                  disabled={
                    pendingSend ||
                    pendingAttachment ||
                    !activeConversation ||
                    composerValue.trim().length === 0
                  }
                  type="submit"
                >
                  {pendingSend ? "Sending..." : "Send"}
                </button>
              </div>
            </form>
          </footer>
        </div>
      </div>

      {detailsPanelMode !== "none" ? (
        <div className="pointer-events-none absolute inset-y-0 right-0 z-20 flex w-full justify-end bg-[rgba(5,9,13,0.36)]">
          <aside className="pointer-events-auto h-full w-full max-w-md border-l border-[var(--line)] bg-[rgba(9,15,21,0.94)] p-5 shadow-[0_0_40px_rgba(0,0,0,0.32)] backdrop-blur-xl">
            <div className="flex items-center justify-between gap-3">
              <div>
                <p className="text-sm font-semibold text-[var(--foreground)]">
                  {detailsPanelMode === "settings"
                    ? "Settings"
                    : detailsPanelMode === "profile"
                      ? "Profile"
                      : "Group details"}
                </p>
                <p className="text-xs text-[var(--muted)]">
                  {detailsPanelMode === "settings"
                    ? "Manage your profile."
                    : detailsPanelMode === "profile"
                      ? "Inspect this Loomic contact."
                      : "Manage this Loomic group."}
                </p>
              </div>
              <button
                className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]"
                onClick={() => setDetailsPanelMode("none")}
                type="button"
              >
                Close
              </button>
            </div>

            <div className="token-log mt-5 h-[calc(100%-4.5rem)] overflow-y-auto pr-1">
              {detailsPanelMode === "settings" ? (
                <div className="space-y-6">
                  <form
                    className="rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-4"
                    onSubmit={handleSaveProfile}
                  >
                    <h3 className="text-sm font-medium text-[var(--foreground)]">Your profile</h3>
                    <div className="mt-4 space-y-3">
                      <div>
                        <label className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                          Username
                        </label>
                        <p className="mt-2 text-sm text-[var(--foreground)]">
                          {session.username}
                        </p>
                      </div>
                      <div>
                        <label className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                          Bio
                        </label>
                        <textarea
                          className="mt-2 min-h-24 w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                          onChange={(event) => setSettingsBio(event.target.value)}
                          placeholder="Tell Loomic who you are"
                          value={settingsBio}
                        />
                      </div>
                      <div>
                        <label className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                          Avatar URL
                        </label>
                        <div className="mt-2 flex items-center gap-3">
                          <Avatar
                            alt={`${session.username} avatar preview`}
                            className="h-14 w-14 text-base font-semibold"
                            name={session.username}
                            src={settingsAvatarUrl}
                          />
                          <input
                            className="min-w-0 flex-1 rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                            onChange={(event) => setSettingsAvatarUrl(event.target.value)}
                            placeholder="https://example.com/avatar.jpg"
                            value={settingsAvatarUrl}
                          />
                        </div>
                      </div>
                    </div>
                    {settingsNotice ? (
                      <div className="mt-3 rounded-2xl border border-[rgba(114,214,201,0.26)] bg-[rgba(114,214,201,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                        {settingsNotice}
                      </div>
                    ) : null}
                    {settingsError ? (
                      <div className="mt-3 rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                        {settingsError}
                      </div>
                    ) : null}
                    <button
                      className="mt-4 inline-flex min-h-11 w-full items-center justify-center rounded-2xl bg-[rgba(114,214,201,0.12)] px-4 text-sm font-medium text-[var(--foreground)] hover:bg-[rgba(114,214,201,0.2)] disabled:cursor-not-allowed disabled:opacity-60"
                      disabled={savingSettings}
                      type="submit"
                    >
                      {savingSettings ? "Saving..." : "Save profile"}
                    </button>
                  </form>
                </div>
              ) : null}

              {detailsPanelMode === "profile" && activeConversation?.kind === "dm" ? (
                <div className="space-y-4 rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-4">
                  <div className="flex items-center gap-4">
                    <Avatar
                      alt={`${activeConversation.title} avatar`}
                      className="h-14 w-14 text-base font-semibold"
                      name={activeConversation.title}
                      src={activeProfile?.avatar_url || activeConversation.peerAvatar}
                    />
                    <div>
                      <p className="text-base font-semibold text-[var(--foreground)]">
                        {activeConversation.title}
                      </p>
                      <p className="text-sm text-[var(--muted)]">
                        {formatPresence(activePresence)}
                      </p>
                    </div>
                  </div>
                  <div>
                    <p className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                      Bio
                    </p>
                    <p className="mt-2 text-sm text-[var(--foreground)]">
                      {activeProfile?.bio || activeConversation.peerBio || "No bio available."}
                    </p>
                  </div>
                  <div>
                    <p className="text-xs uppercase tracking-[0.18em] text-[var(--muted)]">
                      User ID
                    </p>
                    <p className="mt-2 break-all text-sm text-[var(--foreground)]">
                      {activeConversation.peerId}
                    </p>
                  </div>
                </div>
              ) : null}

              {detailsPanelMode === "group" && activeConversation?.kind === "group" ? (
                <div className="space-y-6">
                  <form
                    className="rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-4"
                    onSubmit={handleRenameGroup}
                  >
                    <div className="flex items-center justify-between gap-3">
                      <h3 className="text-sm font-medium text-[var(--foreground)]">
                        Group name
                      </h3>
                      <span className="text-xs text-[var(--muted)]">
                        {canManageActiveGroup ? "Admin" : "Member"}
                      </span>
                    </div>
                    <input
                      className="mt-4 w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                      disabled={!canManageActiveGroup || renamingGroup}
                      onChange={(event) => setRenamingGroupName(event.target.value)}
                      placeholder="Rename group"
                      value={renamingGroupName}
                    />
                    <button
                      className="mt-4 inline-flex min-h-11 w-full items-center justify-center rounded-2xl bg-[rgba(114,214,201,0.12)] px-4 text-sm font-medium text-[var(--foreground)] hover:bg-[rgba(114,214,201,0.2)] disabled:cursor-not-allowed disabled:opacity-60"
                      disabled={!canManageActiveGroup || renamingGroup}
                      type="submit"
                    >
                      {renamingGroup ? "Saving..." : "Rename group"}
                    </button>
                  </form>

                  <div className="rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-4">
                    <h3 className="text-sm font-medium text-[var(--foreground)]">Members</h3>
                    <p className="mt-1 text-xs text-[var(--muted)]">
                      {activeGroupDetails?.members.length ?? 0} total members
                    </p>

                    {canManageActiveGroup ? (
                      <>
                        <input
                          className="mt-4 w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                          onChange={(event) => setMemberSearchQuery(event.target.value)}
                          placeholder="Search users to add"
                          value={memberSearchQuery}
                        />

                        {deferredMemberSearchQuery.trim().length >= 2 ? (
                          <div className="token-log mt-3 max-h-40 space-y-2 overflow-y-auto pr-1">
                            {memberSearchPending ? (
                              <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">
                                Searching...
                              </div>
                            ) : null}
                            {!memberSearchPending && memberSearchError ? (
                              <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">
                                {memberSearchError}
                              </div>
                            ) : null}
                            {memberSearchResults.map((user) => (
                              <div
                                key={user.id}
                                className="flex items-center justify-between gap-3 rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3"
                              >
                                <p className="truncate text-sm text-[var(--foreground)]">
                                  {user.username}
                                </p>
                                <button
                                  className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)] disabled:cursor-not-allowed disabled:opacity-60"
                                  disabled={addingGroupMemberId === user.id}
                                  onClick={() => {
                                    void handleAddGroupMember(user);
                                  }}
                                  type="button"
                                >
                                  {addingGroupMemberId === user.id ? "..." : "Add"}
                                </button>
                              </div>
                            ))}
                          </div>
                        ) : null}
                      </>
                    ) : null}

                    <div className="mt-4 space-y-2">
                      {activeGroupDetails?.members.map((member) => (
                        <div
                          key={member.user_id}
                          className="flex items-center justify-between gap-3 rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3"
                        >
                          <div className="min-w-0">
                            <p className="truncate text-sm text-[var(--foreground)]">
                              {member.username}
                            </p>
                            <p className="text-xs text-[var(--muted)]">{member.role}</p>
                          </div>
                          {canManageActiveGroup || member.user_id === selfUserId ? (
                            <button
                              className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)] disabled:cursor-not-allowed disabled:opacity-60"
                              disabled={removingGroupMemberId === member.user_id}
                              onClick={() => {
                                void handleRemoveGroupMember(member);
                              }}
                              type="button"
                            >
                              {removingGroupMemberId === member.user_id ? "..." : "Remove"}
                            </button>
                          ) : null}
                        </div>
                      ))}
                    </div>
                  </div>
                </div>
              ) : null}
            </div>
          </aside>
        </div>
      ) : null}
    </section>
  );
}
