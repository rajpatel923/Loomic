"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { clearStoredSession, readStoredSession, type StoredSession } from "@/lib/session";

const PLACEHOLDER_MESSAGES = [
  {
    sender: "Loomic",
    body: "This is the chat placeholder screen. Live messages will show up here once the backend is connected.",
  },
  {
    sender: "You",
    body: "The layout just needs to show a simple conversation area for now.",
  },
  {
    sender: "Loomic",
    body: "Perfect. This page is visual-only, with no messaging functionality yet.",
  },
] as const;

export default function ChatPlaceholder() {
  const router = useRouter();
  const [session] = useState<StoredSession | null>(() => readStoredSession());

  useEffect(() => {
    if (!session) {
      router.replace("/");
    }
  }, [router, session]);

  function handleSignOut() {
    clearStoredSession();
    router.replace("/");
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
    <section className="mx-auto w-full max-w-4xl section-fade">
      <div className="surface-panel overflow-hidden rounded-[1.75rem]">
        <header className="flex flex-col gap-4 border-b border-[rgba(255,255,255,0.08)] px-5 py-5 sm:flex-row sm:items-center sm:justify-between sm:px-6">
          <div>
            <p className="eyebrow text-[10px] text-[var(--accent)]">Simple Chat</p>
            <h1 className="mt-2 text-2xl text-[var(--foreground)]">Loomic</h1>
            <p className="mt-1 text-sm text-[var(--muted)]">Signed in as {session.username}</p>
          </div>
          <button
            className="inline-flex min-h-11 items-center justify-center rounded-full border border-[rgba(241,205,146,0.3)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.08)]"
            onClick={handleSignOut}
            type="button"
          >
            Sign Out
          </button>
        </header>

        <div className="space-y-4 px-5 py-5 sm:px-6">
          {PLACEHOLDER_MESSAGES.map((message) => {
            const isUser = message.sender === "You";

            return (
              <article
                key={`${message.sender}-${message.body}`}
                className={`max-w-2xl rounded-[1.25rem] border px-4 py-3 text-sm leading-6 ${
                  isUser
                    ? "ml-auto border-[rgba(241,205,146,0.2)] bg-[rgba(241,205,146,0.08)]"
                    : "border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)]"
                }`}
              >
                <p className="eyebrow text-[10px] text-[var(--muted)]">{message.sender}</p>
                <p className="mt-2 text-[var(--foreground)]">{message.body}</p>
              </article>
            );
          })}
        </div>

        <footer className="border-t border-[rgba(255,255,255,0.08)] px-5 py-5 sm:px-6">
          <div className="rounded-[1.25rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] p-4">
            <p className="text-sm text-[rgba(245,236,220,0.7)]">
              Message input placeholder...
            </p>
          </div>
          <div className="mt-3 flex justify-end">
            <button
              className="rounded-full bg-[rgba(241,205,146,0.14)] px-4 py-2 text-sm text-[var(--foreground)] opacity-70"
              type="button"
            >
              Send
            </button>
          </div>
        </footer>
      </div>
    </section>
  );
}
