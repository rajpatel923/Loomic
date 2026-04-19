type UserSearchResult = {
  id: string;
  username: string;
};

type ConversationItem = {
  convId: string;
  userId: string;
  username: string;
  lastMessage?: {
    content: string;
    timestampMs: number;
  } | null;
};

type ConversationMessage = {
  id: string;
  senderId: string;
  content: string;
  timestampMs: number;
  direction: "incoming" | "outgoing";
  pending?: boolean;
  pendingVisible?: boolean;
};

type SimpleChatLayoutProps = {
  sessionUsername: string;
  transportNotice: string | null;
  conversationsError: string | null;
  historyError: string | null;
  conversationActionError: string | null;
  sendError: string | null;
  userSearchQuery: string;
  threadSearch: string;
  userSearchPending: boolean;
  userSearchError: string | null;
  showUserSearchResults: boolean;
  userSearchResults: UserSearchResult[];
  creatingConversationId: string | null;
  conversationsLoading: boolean;
  conversations: ConversationItem[];
  activeConversationId: string;
  activeConversationName: string | null;
  activeMessages: ConversationMessage[];
  activeHistoryLoading: boolean;
  activeHistoryHasMore: boolean;
  activeHistoryLoadingMore: boolean;
  composerValue: string;
  pendingSend: boolean;
  onSignOut: () => void;
  onUserSearchChange: (value: string) => void;
  onThreadSearchChange: (value: string) => void;
  onRefreshConversations: () => void;
  onCreateConversation: (user: UserSearchResult) => void;
  onSelectConversation: (convId: string) => void;
  onLoadOlderMessages: () => void;
  onComposerChange: (value: string) => void;
  onSend: (event: React.FormEvent<HTMLFormElement>) => void;
  formatClock: (timestampMs: number) => string;
  formatRelativeTime: (timestampMs: number) => string;
};

function getInitials(value: string) {
  const parts = value.trim().split(/\s+/).filter(Boolean);

  if (parts.length === 0) {
    return "LM";
  }

  return parts
    .slice(0, 2)
    .map((part) => part[0]?.toUpperCase() ?? "")
    .join("")
    .slice(0, 2);
}

export default function SimpleChatLayout({
  sessionUsername,
  transportNotice,
  conversationsError,
  historyError,
  conversationActionError,
  sendError,
  userSearchQuery,
  threadSearch,
  userSearchPending,
  userSearchError,
  showUserSearchResults,
  userSearchResults,
  creatingConversationId,
  conversationsLoading,
  conversations,
  activeConversationId,
  activeConversationName,
  activeMessages,
  activeHistoryLoading,
  activeHistoryHasMore,
  activeHistoryLoadingMore,
  composerValue,
  pendingSend,
  onSignOut,
  onUserSearchChange,
  onThreadSearchChange,
  onRefreshConversations,
  onCreateConversation,
  onSelectConversation,
  onLoadOlderMessages,
  onComposerChange,
  onSend,
  formatClock,
  formatRelativeTime,
}: SimpleChatLayoutProps) {
  function handleComposerKeyDown(event: React.KeyboardEvent<HTMLTextAreaElement>) {
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
  }

  return (
    <section className="section-fade flex h-full min-h-0 flex-col overflow-hidden">
      <div className="surface-panel grid h-full min-h-0 overflow-hidden sm:rounded-[28px] lg:grid-cols-[320px_minmax(0,1fr)]">
        <aside className="flex min-h-0 flex-col overflow-hidden border-b border-[var(--line)] bg-[rgba(6,12,18,0.62)] lg:border-b-0 lg:border-r">
          <div className="shrink-0 border-b border-[var(--line)] px-4 py-4">
            <div className="flex items-center justify-between gap-3">
              <div className="min-w-0">
                <p className="truncate text-base font-semibold text-[var(--foreground)]">Loomic</p>
                <p className="truncate text-sm text-[var(--muted)]">{sessionUsername}</p>
              </div>
              <button className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]" onClick={onSignOut} type="button">
                Sign out
              </button>
            </div>

            <input
              className="mt-4 w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
              onChange={(event) => onUserSearchChange(event.target.value)}
              placeholder="New chat"
              value={userSearchQuery}
            />

            {showUserSearchResults ? (
              <div className="token-log mt-3 max-h-52 space-y-2 overflow-y-auto">
                {userSearchPending ? <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">Searching...</div> : null}
                {!userSearchPending && userSearchError ? <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">{userSearchError}</div> : null}
                {!userSearchPending && !userSearchError && userSearchResults.length === 0 ? <div className="rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--muted)]">No matches</div> : null}
                {userSearchResults.map((user) => (
                  <div key={user.id} className="flex items-center justify-between gap-3 rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3">
                    <div className="flex min-w-0 items-center gap-3">
                      <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-full bg-[var(--accent-soft)] text-xs font-semibold text-[var(--accent)]">
                        {getInitials(user.username)}
                      </div>
                      <p className="truncate text-sm text-[var(--foreground)]">{user.username}</p>
                    </div>
                      <button
                        className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)] disabled:cursor-not-allowed disabled:opacity-60"
                      disabled={creatingConversationId === user.id}
                      onClick={() => onCreateConversation(user)}
                      type="button"
                    >
                      {creatingConversationId === user.id ? "..." : "Open"}
                    </button>
                  </div>
                ))}
                {conversationActionError ? <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">{conversationActionError}</div> : null}
              </div>
            ) : null}
          </div>

          <div className="shrink-0 border-b border-[var(--line)] px-4 py-3">
            <div className="flex items-center gap-2">
              <input
                className="w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--foreground)] outline-none"
                onChange={(event) => onThreadSearchChange(event.target.value)}
                placeholder="Search conversations"
                value={threadSearch}
              />
              <button className="rounded-xl border border-[var(--line)] bg-[var(--panel)] px-3 py-3 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)]" onClick={onRefreshConversations} type="button">
                Refresh
              </button>
            </div>
          </div>

          {conversationsError ? <div className="px-4 pt-3"><div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">{conversationsError}</div></div> : null}

          <div className="min-h-0 flex-1 overflow-hidden p-2">
            <div className="token-log h-full overflow-y-auto pr-1">
              {conversationsLoading && conversations.length === 0 ? <div className="rounded-2xl px-3 py-3 text-sm text-[var(--muted)]">Loading...</div> : null}
              {!conversationsLoading && conversations.length === 0 ? <div className="rounded-2xl px-3 py-3 text-sm text-[var(--muted)]">No conversations</div> : null}
              {conversations.map((conversation) => (
                <button
                  key={conversation.convId}
                    className={`mb-1.5 flex w-full items-center gap-3 rounded-2xl px-3 py-3 text-left ${conversation.convId === activeConversationId ? "bg-[var(--panel)] shadow-sm ring-1 ring-[var(--line)]" : "hover:bg-[rgba(255,255,255,0.04)]"}`}
                  onClick={() => onSelectConversation(conversation.convId)}
                  type="button"
                >
                    <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-full bg-[rgba(255,255,255,0.08)] text-xs font-semibold text-[var(--foreground)]">
                    {getInitials(conversation.username)}
                  </div>
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center justify-between gap-3">
                      <p className="truncate text-sm font-medium text-[var(--foreground)]">{conversation.username}</p>
                      <span className="shrink-0 text-xs text-[var(--muted)]">
                        {conversation.lastMessage ? formatRelativeTime(conversation.lastMessage.timestampMs) : "New"}
                      </span>
                    </div>
                    <p className="mt-1 truncate text-sm text-[var(--muted)]">
                      {conversation.lastMessage?.content ?? "No messages yet"}
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
                <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-full bg-[rgba(255,255,255,0.08)] text-xs font-semibold text-[var(--foreground)]">
                  {getInitials(activeConversationName ?? "Messages")}
                </div>
                <div className="min-w-0">
                  <h2 className="truncate text-sm font-semibold text-[var(--foreground)] sm:text-base">{activeConversationName ?? "Messages"}</h2>
                </div>
              </div>

              {activeConversationName && activeHistoryHasMore ? (
                <button className="rounded-xl border border-[var(--line)] bg-[var(--panel-subtle)] px-3 py-2 text-sm text-[var(--foreground)] hover:bg-[rgba(255,255,255,0.06)] disabled:cursor-not-allowed disabled:opacity-60" disabled={activeHistoryLoadingMore} onClick={onLoadOlderMessages} type="button">
                  {activeHistoryLoadingMore ? "Loading..." : "Load older"}
                </button>
              ) : null}
            </div>
          </header>

          {transportNotice ? <div className="shrink-0 border-b border-[rgba(245,143,124,0.18)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)] sm:px-6">{transportNotice}</div> : null}
          {historyError ? <div className="shrink-0 border-b border-[rgba(245,143,124,0.18)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)] sm:px-6">{historyError}</div> : null}

          <div className="min-h-0 flex-1 overflow-hidden">
            <div className="token-log h-full overflow-y-auto px-4 py-4 sm:px-6">
              <div className="mx-auto flex min-h-full w-full max-w-4xl flex-col gap-3">
                {activeConversationName && activeHistoryLoading && activeMessages.length === 0 ? <div className="flex h-full min-h-[240px] items-center justify-center text-sm text-[var(--muted)]">Loading...</div> : null}
                {!activeConversationName ? <div className="flex h-full min-h-[240px] items-center justify-center rounded-[28px] border border-dashed border-[var(--line)] bg-[var(--panel-subtle)] px-6 text-center text-sm text-[var(--muted)]">Select a conversation</div> : null}
                {activeConversationName && activeMessages.length === 0 && !activeHistoryLoading ? <div className="flex h-full min-h-[240px] items-center justify-center rounded-[28px] border border-dashed border-[var(--line)] bg-[var(--panel-subtle)] px-6 text-center text-sm text-[var(--muted)]">No messages yet</div> : null}
                {activeMessages.map((message) => {
                  const isOutgoing = message.direction === "outgoing";
                  return (
                    <div key={message.id} className={`flex ${isOutgoing ? "justify-end" : "justify-start"}`}>
                      <article className={`max-w-[85%] rounded-[22px] px-4 py-3 text-sm sm:max-w-[75%] ${isOutgoing ? "bg-[rgba(241,205,146,0.14)] text-[var(--foreground)] border border-[rgba(241,205,146,0.22)]" : "border border-[var(--line)] bg-[var(--panel-subtle)] text-[var(--foreground)]"}`}>
                        <p className="whitespace-pre-wrap leading-6">{message.content}</p>
                        <div className={`mt-2 text-[11px] ${isOutgoing ? "text-[rgba(245,236,220,0.62)]" : "text-[var(--muted)]"}`}>
                          {formatClock(message.timestampMs)}
                          {message.pendingVisible ? " Sending..." : ""}
                        </div>
                      </article>
                    </div>
                  );
                })}
              </div>
            </div>
          </div>

          <footer className="shrink-0 border-t border-[var(--line)] px-4 py-4 sm:px-6">
            {sendError ? <div className="mb-3 rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-3 py-3 text-sm text-[var(--foreground)]">{sendError}</div> : null}
            <form onSubmit={onSend}>
              <div className="flex items-end gap-3 rounded-[24px] border border-[var(--line)] bg-[var(--panel-subtle)] p-3">
                <textarea
                  className="min-h-11 flex-1 resize-none bg-transparent px-1 py-2 text-sm leading-6 text-[var(--foreground)] outline-none"
                  onChange={(event) => onComposerChange(event.target.value)}
                  onKeyDown={handleComposerKeyDown}
                  placeholder={activeConversationName ? `Message ${activeConversationName}` : "Choose a conversation"}
                  value={composerValue}
                />
                <button
                  className="inline-flex min-h-11 items-center justify-center rounded-2xl bg-[rgba(114,214,201,0.12)] px-4 text-sm font-medium text-[var(--foreground)] hover:bg-[rgba(114,214,201,0.2)] disabled:cursor-not-allowed disabled:opacity-50"
                  disabled={pendingSend || !activeConversationName || composerValue.trim().length === 0}
                  type="submit"
                >
                  {pendingSend ? "Sending..." : "Send"}
                </button>
              </div>
            </form>
          </footer>
        </div>
      </div>
    </section>
  );
}
