"use client";

import {
  startTransition,
  useDeferredValue,
  useEffect,
  useMemo,
  useState,
} from "react";
import { useRouter } from "next/navigation";

import type {
  ChatConnectionSnapshot,
  ChatRecord,
  ChatSnapshotPayload,
  ChatStatusPayload,
  ChatTransportErrorPayload,
} from "@/lib/chat-types";
import {
  clearStoredSession,
  readStoredSession,
  type StoredSession,
} from "@/lib/session";

type ThreadSummary = {
  peerId: string;
  lastMessage: ChatRecord | null;
  messageCount: number;
};

const EMPTY_MESSAGES: ChatRecord[] = [];

function formatStatusTone(state: ChatConnectionSnapshot["state"]) {
  if (state === "connected") {
    return "text-[var(--signal)]";
  }

  if (state === "error") {
    return "text-[var(--danger)]";
  }

  if (state === "reconnecting" || state === "connecting") {
    return "text-[var(--accent)]";
  }

  return "text-[var(--muted)]";
}

function formatStatusLabel(state: ChatConnectionSnapshot["state"]) {
  switch (state) {
    case "connected":
      return "Connected";
    case "connecting":
      return "Connecting";
    case "reconnecting":
      return "Reconnecting";
    case "error":
      return "Needs Attention";
    case "disconnected":
      return "Disconnected";
    default:
      return "Idle";
  }
}

function formatClock(timestampMs: number) {
  return new Intl.DateTimeFormat("en-US", {
    hour: "numeric",
    minute: "2-digit",
  }).format(timestampMs);
}

function formatRelativeTime(timestampMs: number) {
  const minutesAgo = Math.max(
    0,
    Math.round((Date.now() - timestampMs) / 60_000),
  );

  if (minutesAgo < 1) {
    return "just now";
  }

  if (minutesAgo === 1) {
    return "1 min ago";
  }

  if (minutesAgo < 60) {
    return `${minutesAgo} min ago`;
  }

  const hoursAgo = Math.round(minutesAgo / 60);
  return `${hoursAgo} hr ago`;
}

function upsertMessages(previous: ChatRecord[], incoming: ChatRecord[]) {
  const next = [...previous];

  for (const message of incoming) {
    const index = next.findIndex((candidate) => candidate.id === message.id);

    if (index >= 0) {
      next[index] = message;
      continue;
    }

    next.push(message);
  }

  return next.toSorted((left, right) => left.timestampMs - right.timestampMs);
}

export default function ChatPlaceholder() {
  const router = useRouter();
  const [session] = useState<StoredSession | null>(() => readStoredSession());
  const [messages, setMessages] = useState<ChatRecord[]>([]);
  const [status, setStatus] = useState<ChatConnectionSnapshot | null>(null);
  const [activePeerId, setActivePeerId] = useState("");
  const [recipientDraft, setRecipientDraft] = useState("");
  const [composerValue, setComposerValue] = useState("");
  const [transportNotice, setTransportNotice] = useState<string | null>(null);
  const [sendError, setSendError] = useState<string | null>(null);
  const [pendingSend, setPendingSend] = useState(false);
  const [threadSearch, setThreadSearch] = useState("");
  const deferredThreadSearch = useDeferredValue(threadSearch);

  useEffect(() => {
    if (!session) {
      router.replace("/");
    }
  }, [router, session]);

  useEffect(() => {
    if (!session) {
      return;
    }

    const currentSession = session;
    let disposed = false;
    let eventSource: EventSource | null = null;

    async function connect() {
      setTransportNotice(null);

      const connectResponse = await fetch("/api/chat/connect", {
        method: "POST",
        headers: {
          "content-type": "application/json",
        },
        body: JSON.stringify({
          access_token: currentSession.access_token,
          client_session_id: currentSession.client_session_id,
        }),
      });

      const connectPayload = (await connectResponse.json().catch(() => null)) as
        | { error?: string; status?: ChatConnectionSnapshot }
        | null;

      if (!connectResponse.ok) {
        throw new Error(
          connectPayload?.error ??
            "Unable to open the secure Loomic chat bridge.",
        );
      }

      if (disposed) {
        return;
      }

      if (connectPayload?.status) {
        setStatus(connectPayload.status);
      }

      eventSource = new EventSource(
        `/api/chat/events?clientSessionId=${encodeURIComponent(currentSession.client_session_id)}`,
      );

      eventSource.addEventListener("snapshot", (event) => {
        const payload = JSON.parse(
          (event as MessageEvent<string>).data,
        ) as ChatSnapshotPayload;

        startTransition(() => {
          setStatus(payload.status);
          setMessages(payload.messages);
          setActivePeerId((previous) => {
            if (previous) {
              return previous;
            }

            const newestPeer = payload.messages.at(-1);

            if (!newestPeer) {
              return previous;
            }

            return newestPeer.direction === "outgoing"
              ? newestPeer.recipientId
              : newestPeer.senderId;
          });
        });
      });

      eventSource.addEventListener("status", (event) => {
        const payload = JSON.parse(
          (event as MessageEvent<string>).data,
        ) as ChatStatusPayload;
        setStatus(payload.status);
      });

      eventSource.addEventListener("message", (event) => {
        const payload = JSON.parse(
          (event as MessageEvent<string>).data,
        ) as ChatRecord;

        startTransition(() => {
          setMessages((previous) => upsertMessages(previous, [payload]));
          setActivePeerId((previous) => {
            if (previous) {
              return previous;
            }

            return payload.direction === "outgoing"
              ? payload.recipientId
              : payload.senderId;
          });
        });
      });

      eventSource.addEventListener("transport-error", (event) => {
        const payload = JSON.parse(
          (event as MessageEvent<string>).data,
        ) as ChatTransportErrorPayload;
        setTransportNotice(payload.message);
      });

      eventSource.onerror = () => {
        if (!disposed) {
          setTransportNotice(
            "The browser lost its live event stream. Loomic will keep retrying.",
          );
        }
      };
    }

    connect().catch((error) => {
      if (!disposed) {
        setTransportNotice(
          error instanceof Error ? error.message : "Unable to start Loomic chat.",
        );
      }
    });

    return () => {
      disposed = true;
      eventSource?.close();
    };
  }, [session]);

  async function handleSignOut() {
    if (session) {
      await fetch("/api/chat/disconnect", {
        method: "POST",
        headers: {
          "content-type": "application/json",
        },
        body: JSON.stringify({
          client_session_id: session.client_session_id,
        }),
      }).catch(() => undefined);
    }

    clearStoredSession();
    router.replace("/");
  }

  const threads = useMemo(() => {
    const summaries = new Map<string, ThreadSummary>();

    for (const message of messages) {
      const peerId =
        message.direction === "outgoing"
          ? message.recipientId
          : message.senderId;

      const previous = summaries.get(peerId);
      summaries.set(peerId, {
        peerId,
        lastMessage:
          !previous || !previous.lastMessage
            ? message
            : previous.lastMessage.timestampMs <= message.timestampMs
              ? message
              : previous.lastMessage,
        messageCount: (previous?.messageCount ?? 0) + 1,
      });
    }

    if (activePeerId && !summaries.has(activePeerId)) {
      summaries.set(activePeerId, {
        peerId: activePeerId,
        lastMessage: null,
        messageCount: 0,
      });
    }

    return [...summaries.values()].toSorted((left, right) => {
      const leftTime = left.lastMessage?.timestampMs ?? 0;
      const rightTime = right.lastMessage?.timestampMs ?? 0;
      return rightTime - leftTime;
    });
  }, [activePeerId, messages]);

  const visibleThreads = useMemo(() => {
    const query = deferredThreadSearch.trim().toLowerCase();

    if (!query) {
      return threads;
    }

    return threads.filter((thread) => {
      const lastPreview = thread.lastMessage?.content.toLowerCase() ?? "";
      return (
        thread.peerId.toLowerCase().includes(query) || lastPreview.includes(query)
      );
    });
  }, [deferredThreadSearch, threads]);

  const activeMessages = activePeerId
    ? messages.filter((message) => {
        const peerId =
          message.direction === "outgoing"
            ? message.recipientId
            : message.senderId;
        return peerId === activePeerId;
      })
    : EMPTY_MESSAGES;

  function handleOpenThread(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setSendError(null);

    const nextPeerId = recipientDraft.trim();

    if (!/^\d+$/.test(nextPeerId)) {
      setSendError("Recipient ids need to be numeric Snowflake ids.");
      return;
    }

    startTransition(() => {
      setActivePeerId(nextPeerId);
      setRecipientDraft("");
    });
  }

  async function handleSend(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();
    setSendError(null);

    if (!session || !activePeerId) {
      setSendError("Pick or create a thread before sending.");
      return;
    }

    const content = composerValue.trim();

    if (!content) {
      setSendError("Write a message before sending.");
      return;
    }

    setPendingSend(true);

    try {
      const response = await fetch("/api/chat/send", {
        method: "POST",
        headers: {
          "content-type": "application/json",
        },
        body: JSON.stringify({
          access_token: session.access_token,
          client_session_id: session.client_session_id,
          recipient_id: activePeerId,
          content,
        }),
      });

      const payload = (await response.json().catch(() => null)) as
        | { error?: string }
        | null;

      if (!response.ok) {
        throw new Error(payload?.error ?? "Unable to send the Loomic message.");
      }

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

  if (!session) {
    return (
      <section className="mx-auto w-full max-w-3xl section-fade">
        <div className="surface-panel rounded-[1.5rem] px-6 py-10 text-center sm:px-7">
          <p className="eyebrow text-[10px] text-[var(--accent)]">Loomic Chat</p>
          <p className="mt-4 text-sm text-[var(--muted)]">Loading your workspace...</p>
        </div>
      </section>
    );
  }

  return (
    <section className="mx-auto w-full max-w-6xl section-fade">
      <div className="surface-panel overflow-hidden rounded-[2rem]">
        <header className="border-b border-[rgba(255,255,255,0.08)] px-5 py-5 sm:px-6">
          <div className="flex flex-col gap-5 lg:flex-row lg:items-end lg:justify-between">
            <div className="space-y-3">
              <p className="eyebrow text-[10px] text-[var(--accent)]">
                Loomic Relay
              </p>
              <div>
                <h1 className="font-display text-4xl leading-none tracking-[-0.05em] text-[var(--foreground)] sm:text-5xl">
                  Live TLS chat
                </h1>
                <p className="mt-3 max-w-2xl text-sm leading-6 text-[var(--muted)]">
                  The browser now rides through a secure Next.js bridge into the
                  backend TCP server. Messages stream live, reconnect
                  automatically, and queued deliveries appear as soon as the
                  backend flushes them.
                </p>
              </div>
            </div>

            <div className="grid gap-3 sm:grid-cols-2">
              <div className="rounded-[1.25rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3">
                <p className="eyebrow text-[10px] text-[var(--muted)]">Signed in</p>
                <p className="mt-2 text-sm text-[var(--foreground)]">
                  {session.username}
                </p>
                <p className="mt-1 font-mono text-xs text-[rgba(245,236,220,0.58)]">
                  Your id: {status?.selfUserId ?? session.user_id ?? "waiting..."}
                </p>
              </div>
              <div className="rounded-[1.25rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3">
                <p className="eyebrow text-[10px] text-[var(--muted)]">Bridge status</p>
                <div className="mt-2 flex items-center gap-3">
                  <span
                    className={`status-dot ${formatStatusTone(status?.state ?? "idle")}`}
                  />
                  <div>
                    <p className="text-sm text-[var(--foreground)]">
                      {formatStatusLabel(status?.state ?? "idle")}
                    </p>
                    <p className="text-xs text-[rgba(245,236,220,0.58)]">
                      {status?.detail ?? "Preparing the secure messaging session."}
                    </p>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </header>

        <div className="grid gap-0 lg:grid-cols-[320px_minmax(0,1fr)]">
          <aside className="border-b border-[rgba(255,255,255,0.08)] p-5 lg:border-b-0 lg:border-r lg:p-6">
            <div className="rounded-[1.4rem] border border-[rgba(241,205,146,0.22)] bg-[rgba(241,205,146,0.05)] p-4">
              <p className="eyebrow text-[10px] text-[var(--accent)]">
                New Thread
              </p>
              <p className="mt-3 text-sm leading-6 text-[var(--muted)]">
                The backend does not expose a user directory yet, so start
                conversations with a recipient Snowflake id.
              </p>
              <form className="mt-4 space-y-3" onSubmit={handleOpenThread}>
                <input
                  className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-sm text-[var(--foreground)]"
                  inputMode="numeric"
                  onChange={(event) => setRecipientDraft(event.target.value)}
                  placeholder="Recipient id"
                  value={recipientDraft}
                />
                <button
                  className="inline-flex min-h-11 w-full items-center justify-center rounded-full bg-[rgba(241,205,146,0.14)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.22)]"
                  type="submit"
                >
                  Open Conversation
                </button>
              </form>
            </div>

            <div className="mt-6">
              <div className="flex items-center justify-between gap-3">
                <p className="eyebrow text-[10px] text-[var(--muted)]">Active threads</p>
                <span className="font-mono text-xs text-[rgba(245,236,220,0.52)]">
                  {threads.length}
                </span>
              </div>
              <input
                className="mt-3 w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-sm text-[var(--foreground)]"
                onChange={(event) => setThreadSearch(event.target.value)}
                placeholder="Search ids or message text"
                value={threadSearch}
              />
              <div className="token-log mt-4 max-h-[28rem] space-y-3 overflow-y-auto pr-1">
                {visibleThreads.length > 0 ? (
                  visibleThreads.map((thread) => {
                    const isActive = thread.peerId === activePeerId;

                    return (
                      <button
                        key={thread.peerId}
                        className={`w-full rounded-[1.3rem] border px-4 py-4 text-left ${
                          isActive
                            ? "border-[rgba(241,205,146,0.28)] bg-[rgba(241,205,146,0.08)]"
                            : "border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] hover:border-[rgba(114,214,201,0.2)] hover:bg-[rgba(114,214,201,0.05)]"
                        }`}
                        onClick={() => setActivePeerId(thread.peerId)}
                        type="button"
                      >
                        <div className="flex items-start justify-between gap-3">
                          <div>
                            <p className="eyebrow text-[10px] text-[var(--muted)]">
                              Recipient
                            </p>
                            <p className="mt-2 font-mono text-sm text-[var(--foreground)]">
                              {thread.peerId}
                            </p>
                          </div>
                          <span className="text-xs text-[rgba(245,236,220,0.5)]">
                            {thread.lastMessage
                              ? formatRelativeTime(thread.lastMessage.timestampMs)
                              : "new"}
                          </span>
                        </div>
                        <p className="mt-3 line-clamp-2 text-sm leading-6 text-[var(--muted)]">
                          {thread.lastMessage?.content ??
                            "No messages yet. The tunnel is ready when you are."}
                        </p>
                      </button>
                    );
                  })
                ) : (
                  <div className="rounded-[1.3rem] border border-dashed border-[rgba(255,255,255,0.08)] px-4 py-6 text-sm leading-6 text-[var(--muted)]">
                    Open a thread with a recipient id to start live messaging.
                  </div>
                )}
              </div>
            </div>
          </aside>

          <div className="flex min-h-[42rem] flex-col">
            <div className="border-b border-[rgba(255,255,255,0.08)] px-5 py-5 sm:px-6">
              <div className="flex flex-col gap-4 md:flex-row md:items-center md:justify-between">
                <div>
                  <p className="eyebrow text-[10px] text-[var(--accent)]">
                    Conversation
                  </p>
                  <h2 className="mt-2 font-mono text-lg text-[var(--foreground)]">
                    {activePeerId || "No recipient selected"}
                  </h2>
                  <p className="mt-1 text-sm text-[var(--muted)]">
                    Session memory keeps recent messages visible here until a
                    dedicated history endpoint lands on the backend.
                  </p>
                </div>
                <button
                  className="inline-flex min-h-11 items-center justify-center rounded-full border border-[rgba(241,205,146,0.3)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.08)]"
                  onClick={handleSignOut}
                  type="button"
                >
                  Sign Out
                </button>
              </div>
            </div>

            <div className="token-log flex-1 space-y-4 overflow-y-auto px-5 py-5 sm:px-6">
              {transportNotice ? (
                <div className="rounded-[1.2rem] border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
                  {transportNotice}
                </div>
              ) : null}

              {activeMessages.length > 0 ? (
                activeMessages.map((message) => {
                  const isOutgoing = message.direction === "outgoing";

                  return (
                    <article
                      key={message.id}
                      className={`max-w-2xl rounded-[1.35rem] border px-4 py-4 text-sm leading-6 ${
                        isOutgoing
                          ? "ml-auto border-[rgba(241,205,146,0.22)] bg-[rgba(241,205,146,0.08)]"
                          : "border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)]"
                      }`}
                    >
                      <div className="flex items-center justify-between gap-4">
                        <p className="eyebrow text-[10px] text-[var(--muted)]">
                          {isOutgoing ? "You" : `User ${message.senderId}`}
                        </p>
                        <span className="text-xs text-[rgba(245,236,220,0.52)]">
                          {formatClock(message.timestampMs)}
                        </span>
                      </div>
                      <p className="mt-3 whitespace-pre-wrap text-[var(--foreground)]">
                        {message.content}
                      </p>
                    </article>
                  );
                })
              ) : (
                <div className="flex h-full min-h-[18rem] items-center justify-center rounded-[1.5rem] border border-dashed border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.02)] px-6 text-center text-sm leading-6 text-[var(--muted)]">
                  {activePeerId
                    ? "The bridge is ready. Send the first message to bring this thread online."
                    : "Choose a thread on the left or enter a recipient id to begin."}
                </div>
              )}
            </div>

            <footer className="border-t border-[rgba(255,255,255,0.08)] px-5 py-5 sm:px-6">
              {sendError ? (
                <div className="mb-4 rounded-[1rem] border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
                  {sendError}
                </div>
              ) : null}

              <form onSubmit={handleSend}>
                <div className="rounded-[1.5rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] p-4">
                  <textarea
                    className="min-h-28 w-full resize-none bg-transparent text-sm leading-6 text-[var(--foreground)] outline-none"
                    onChange={(event) => setComposerValue(event.target.value)}
                    placeholder={
                      activePeerId
                        ? `Message user ${activePeerId}`
                        : "Pick a recipient before composing."
                    }
                    value={composerValue}
                  />
                  <div className="mt-4 flex flex-col gap-3 border-t border-[rgba(255,255,255,0.08)] pt-4 sm:flex-row sm:items-center sm:justify-between">
                    <div className="text-sm text-[var(--muted)]">
                      {status?.connectedAt
                        ? `Secure tunnel live since ${formatClock(status.connectedAt)}`
                        : "Waiting for the secure tunnel to finish connecting."}
                    </div>
                    <button
                      className="inline-flex min-h-11 items-center justify-center rounded-full bg-[rgba(114,214,201,0.12)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(114,214,201,0.2)] disabled:cursor-not-allowed disabled:opacity-50"
                      disabled={
                        pendingSend ||
                        !activePeerId ||
                        composerValue.trim().length === 0
                      }
                      type="submit"
                    >
                      {pendingSend ? "Sending..." : "Send over TLS"}
                    </button>
                  </div>
                </div>
              </form>
            </footer>
          </div>
        </div>

        <footer className="border-t border-[rgba(255,255,255,0.08)] px-5 py-4 sm:px-6">
          <div className="flex flex-col gap-2 text-xs leading-6 text-[rgba(245,236,220,0.56)] sm:flex-row sm:items-center sm:justify-between">
            <p>
              Live messages ride through the Next.js bridge into Loomic&apos;s
              binary TCP protocol on port 9000.
            </p>
            <button
              className="rounded-full border border-[rgba(255,255,255,0.08)] px-4 py-2 text-xs text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.03)]"
              onClick={() => setTransportNotice(null)}
              type="button"
            >
              Clear Notice
            </button>
          </div>
        </footer>
      </div>
    </section>
  );
}
