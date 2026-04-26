"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { decodeJwtExpiry } from "@/lib/jwt";
import {
  clearStoredSession,
  readStoredSession,
  saveStoredSession,
} from "@/lib/session";

type LoginResponse = {
  access_token: string;
  refresh_token: string;
  token_type: string;
};

type AuthError = {
  error?: string;
};

type RegisterResponse = {
  id: string;
  username: string;
};

type AuthView = "login" | "create-account";

const MIN_PASSWORD_LENGTH = 6;

function isValidEmail(value: string) {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value);
}

export default function LoginExperience() {
  const router = useRouter();
  const [view, setView] = useState<AuthView>("login");
  const [email, setEmail] = useState("");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [rememberMe, setRememberMe] = useState(true);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [ready, setReady] = useState(false);

  const trimmedUsername = username.trim();
  const trimmedEmail = email.trim();
  const emailIsValid = trimmedEmail.length > 0 && isValidEmail(trimmedEmail);
  const canCreateAccount =
    trimmedUsername.length > 0 &&
    emailIsValid &&
    password.length >= MIN_PASSWORD_LENGTH;

  useEffect(() => {
    const session = readStoredSession();

    if (session) {
      const expiresAt = decodeJwtExpiry(session.access_token);

      if (expiresAt && expiresAt * 1000 <= Date.now()) {
        clearStoredSession();
        setReady(true);
        return;
      }

      router.replace("/chat");
      return;
    }

    setReady(true);
  }, [router]);

  function switchView(nextView: AuthView) {
    setView(nextView);
    setError(null);
    setNotice(null);
  }

  async function handleSubmit(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault();

    setPending(true);
    setError(null);
    setNotice(null);

    try {
      if (view === "login") {
        const response = await fetch("/api/auth/login", {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
          },
          body: JSON.stringify({
            username,
            password,
          }),
        });

        const payload = (await response.json()) as LoginResponse | AuthError;

        if (!response.ok) {
          setError((payload as AuthError).error ?? "Unable to sign in.");
          return;
        }

        saveStoredSession(
          {
            ...(payload as LoginResponse),
            username,
            persisted: rememberMe,
          },
          rememberMe,
        );

        setPassword("");
        router.push("/chat");
        return;
      }

      const response = await fetch("/api/auth/register", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          username,
          email,
          password,
        }),
      });

      const payload = (await response.json()) as RegisterResponse | AuthError;

      if (!response.ok) {
        setError((payload as AuthError).error ?? "Unable to create account.");
        return;
      }

      setView("login");
      setEmail("");
      setPassword("");
      setNotice(`Account created for ${(payload as RegisterResponse).username}.`);
    } catch {
      setError(
        view === "login"
          ? "The sign-in service is unavailable right now."
          : "The registration service is unavailable right now.",
      );
    } finally {
      setPending(false);
    }
  }

  if (!ready) {
    return (
      <section className="section-fade">
        <div className="surface-panel rounded-[28px] px-6 py-8 text-center">
          <p className="text-sm text-[var(--muted)]">Loading...</p>
        </div>
      </section>
    );
  }

  return (
    <section className="section-fade">
      <div className="surface-panel rounded-[28px] p-6 sm:p-7">
        <div className="mb-6">
          <h1 className="text-2xl font-semibold tracking-[-0.02em] text-[var(--foreground)]">
            Loomic
          </h1>
        </div>

        <div className="grid grid-cols-2 gap-1 rounded-2xl bg-[var(--panel-subtle)] p-1">
          <button
            className={`rounded-[14px] px-4 py-2.5 text-sm ${
              view === "login"
                ? "bg-[var(--panel)] text-[var(--foreground)] shadow-sm"
                : "text-[var(--muted)]"
            }`}
            onClick={() => switchView("login")}
            type="button"
          >
            Login
          </button>
          <button
            className={`rounded-[14px] px-4 py-2.5 text-sm ${
              view === "create-account"
                ? "bg-[var(--panel)] text-[var(--foreground)] shadow-sm"
                : "text-[var(--muted)]"
            }`}
            onClick={() => switchView("create-account")}
            type="button"
          >
            Create account
          </button>
        </div>

        <form className="mt-6 space-y-4" onSubmit={handleSubmit}>
          <div className="space-y-2">
            <label className="text-sm text-[var(--muted)]" htmlFor="username">
              Username
            </label>
            <input
              id="username"
              autoComplete="username"
              className="w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-[var(--foreground)] outline-none"
              onChange={(event) => setUsername(event.target.value)}
              placeholder="Username"
              value={username}
            />
          </div>

          {view === "create-account" ? (
            <div className="space-y-2">
              <label className="text-sm text-[var(--muted)]" htmlFor="email">
                Email
              </label>
              <input
                id="email"
                autoComplete="email"
                className="w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-[var(--foreground)] outline-none"
                onChange={(event) => setEmail(event.target.value)}
                placeholder="Email"
                type="email"
                value={email}
              />
            </div>
          ) : null}

          <div className="space-y-2">
            <label className="text-sm text-[var(--muted)]" htmlFor="password">
              Password
            </label>
            <input
              id="password"
              autoComplete={view === "login" ? "current-password" : "new-password"}
              className="w-full rounded-2xl border border-[var(--line)] bg-[var(--panel)] px-4 py-3 text-[var(--foreground)] outline-none"
              minLength={view === "create-account" ? MIN_PASSWORD_LENGTH : undefined}
              onChange={(event) => setPassword(event.target.value)}
              placeholder="Password"
              type="password"
              value={password}
            />
          </div>

          {view === "login" ? (
            <label className="flex items-center gap-3 text-sm text-[var(--muted)]">
              <input
                checked={rememberMe}
                className="h-4 w-4 accent-[var(--accent)]"
                onChange={(event) => setRememberMe(event.target.checked)}
                type="checkbox"
              />
              Remember me
            </label>
          ) : (
            <p className="text-sm text-[var(--muted)]">
              Password must be at least {MIN_PASSWORD_LENGTH} characters.
            </p>
          )}

          {notice ? (
            <div className="rounded-2xl border border-[rgba(114,214,201,0.26)] bg-[rgba(114,214,201,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
              {notice}
            </div>
          ) : null}

          {error ? (
            <div className="rounded-2xl border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
              {error}
            </div>
          ) : null}

          <button
            className="inline-flex min-h-12 w-full items-center justify-center rounded-2xl bg-[rgba(241,205,146,0.14)] px-5 text-sm font-medium text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.22)] disabled:cursor-not-allowed disabled:opacity-60"
            disabled={
              pending ||
              (view === "login"
                ? trimmedUsername.length === 0 || password.length === 0
                : !canCreateAccount)
            }
            type="submit"
          >
            {pending
              ? view === "login"
                ? "Signing in..."
                : "Creating..."
              : view === "login"
                ? "Sign in"
                : "Create account"}
          </button>
        </form>
      </div>
    </section>
  );
}
