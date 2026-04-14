"use client";

import { useEffect, useState } from "react";

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

type StoredSession = LoginResponse & {
  username: string;
  persisted: boolean;
};

type AuthView = "login" | "create-account";

const SESSION_STORAGE_KEY = "loomic.session";

function readStoredSession() {
  if (typeof window === "undefined") {
    return null;
  }

  const fromSession = window.sessionStorage.getItem(SESSION_STORAGE_KEY);
  const fromLocal = window.localStorage.getItem(SESSION_STORAGE_KEY);
  const raw = fromSession ?? fromLocal;

  if (!raw) {
    return null;
  }

  try {
    return JSON.parse(raw) as StoredSession;
  } catch {
    window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
    window.localStorage.removeItem(SESSION_STORAGE_KEY);
    return null;
  }
}

export default function LoginExperience() {
  const [view, setView] = useState<AuthView>("login");
  const [email, setEmail] = useState("");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [rememberMe, setRememberMe] = useState(true);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [session, setSession] = useState<StoredSession | null>(null);

  useEffect(() => {
    setSession(readStoredSession());
  }, []);

  function switchToLogin() {
    setView("login");
    setError(null);
    setNotice(null);
  }

  function switchToCreateAccount() {
    setView("create-account");
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

        const nextSession: StoredSession = {
          ...(payload as LoginResponse),
          username,
          persisted: rememberMe,
        };

        const serialized = JSON.stringify(nextSession);

        if (rememberMe) {
          window.localStorage.setItem(SESSION_STORAGE_KEY, serialized);
          window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
        } else {
          window.sessionStorage.setItem(SESSION_STORAGE_KEY, serialized);
          window.localStorage.removeItem(SESSION_STORAGE_KEY);
        }

        setSession(nextSession);
        setPassword("");
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
      setNotice(
        `Account created for ${(payload as RegisterResponse).username}. Sign in to continue.`,
      );
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

  function handleSignOut() {
    window.localStorage.removeItem(SESSION_STORAGE_KEY);
    window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
    setSession(null);
    setPassword("");
    setError(null);
    setNotice(null);
  }

  return (
    <section className="surface-panel mx-auto w-full max-w-md rounded-[1.5rem] p-6 sm:p-7">
      <div className="relative z-10 grid grid-cols-2 rounded-full border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] p-1">
        <button
          className={`relative z-10 rounded-full px-4 py-3 text-sm touch-manipulation ${
            view === "login"
              ? "bg-[rgba(241,205,146,0.14)] text-[var(--foreground)]"
              : "text-[var(--muted)]"
          }`}
          onClick={switchToLogin}
          type="button"
        >
          Login
        </button>
        <button
          className={`relative z-10 rounded-full px-4 py-3 text-sm touch-manipulation ${
            view === "create-account"
              ? "bg-[rgba(241,205,146,0.14)] text-[var(--foreground)]"
              : "text-[var(--muted)]"
          }`}
          onClick={switchToCreateAccount}
          type="button"
        >
          Create Account
        </button>
      </div>

      {session ? (
        <div className="mt-6 space-y-4">
          <div className="rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-5">
            <p className="text-lg text-[var(--foreground)]">Signed in as {session.username}</p>
          </div>
          <button
            className="inline-flex min-h-11 w-full items-center justify-center rounded-full border border-[rgba(241,205,146,0.3)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.08)]"
            onClick={handleSignOut}
            type="button"
          >
            Sign Out
          </button>
        </div>
      ) : view === "login" ? (
        <form className="mt-6 space-y-4" onSubmit={handleSubmit}>
          <input
            autoComplete="username"
            className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
            onChange={(event) => setUsername(event.target.value)}
            placeholder="Username"
            value={username}
          />
          <input
            autoComplete="current-password"
            className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
            onChange={(event) => setPassword(event.target.value)}
            placeholder="Password"
            type="password"
            value={password}
          />
          <label className="flex items-center gap-3 text-sm text-[var(--muted)]">
            <input
              checked={rememberMe}
              className="h-4 w-4 accent-[var(--accent)]"
              onChange={(event) => setRememberMe(event.target.checked)}
              type="checkbox"
            />
            Remember me
          </label>
          {notice ? (
            <div className="rounded-[1rem] border border-[rgba(158,214,167,0.28)] bg-[rgba(158,214,167,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
              {notice}
            </div>
          ) : null}
          {error ? (
            <div className="rounded-[1rem] border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
              {error}
            </div>
          ) : null}
          <button
            className="inline-flex min-h-11 w-full items-center justify-center rounded-full bg-[rgba(241,205,146,0.14)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.22)] disabled:cursor-not-allowed disabled:opacity-60"
            disabled={pending || username.trim().length === 0 || password.length === 0}
            type="submit"
          >
            {pending ? "Signing In..." : "Sign In"}
          </button>
          <button
            className="w-full text-sm text-[var(--muted)] underline decoration-[rgba(241,205,146,0.35)] underline-offset-4 hover:text-[var(--foreground)]"
            onClick={switchToCreateAccount}
            type="button"
          >
            Need an account? Create one here.
          </button>
        </form>
      ) : (
        <form className="mt-6 space-y-4" onSubmit={handleSubmit}>
          <input
            autoComplete="username"
            className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
            onChange={(event) => setUsername(event.target.value)}
            placeholder="Username"
            value={username}
          />
          <input
            autoComplete="email"
            className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
            onChange={(event) => setEmail(event.target.value)}
            placeholder="Email"
            type="email"
            value={email}
          />
          <input
            autoComplete="new-password"
            className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
            onChange={(event) => setPassword(event.target.value)}
            placeholder="Password"
            type="password"
            value={password}
          />
          {error ? (
            <div className="rounded-[1rem] border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
              {error}
            </div>
          ) : null}
          <button
            className="inline-flex min-h-11 w-full items-center justify-center rounded-full bg-[rgba(241,205,146,0.14)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.22)] disabled:cursor-not-allowed disabled:opacity-60"
            disabled={
              pending ||
              username.trim().length === 0 ||
              email.trim().length === 0 ||
              password.length < 8
            }
            type="submit"
          >
            {pending ? "Creating Account..." : "Create Account"}
          </button>
          <button
            className="w-full text-sm text-[var(--muted)] underline decoration-[rgba(241,205,146,0.35)] underline-offset-4 hover:text-[var(--foreground)]"
            onClick={switchToLogin}
            type="button"
          >
            Already have an account? Back to login.
          </button>
        </form>
      )}
    </section>
  );
}
