"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { saveStoredSession, readStoredSession } from "@/lib/session";

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

function FieldRequirementNotice({ message }: { message: string | null }) {
  if (!message) {
    return null;
  }

  return (
    <span className="group relative inline-flex items-center" tabIndex={0}>
      <span
        aria-hidden="true"
        className="h-0 w-0 border-l-[6px] border-r-[6px] border-b-[11px] border-l-transparent border-r-transparent border-b-[#f58f7c]"
      />
      <span className="pointer-events-none absolute right-0 top-full z-20 mt-2 hidden w-52 rounded-xl border border-[rgba(245,143,124,0.35)] bg-[rgba(27,27,31,0.96)] px-3 py-2 text-xs leading-5 text-[var(--foreground)] shadow-[0_12px_30px_rgba(0,0,0,0.28)] group-hover:block group-focus-within:block">
        {message}
      </span>
    </span>
  );
}

function AuthPanel({
  children,
  view,
  onSwitchToLogin,
  onSwitchToCreateAccount,
}: {
  children: React.ReactNode;
  view: AuthView;
  onSwitchToLogin: () => void;
  onSwitchToCreateAccount: () => void;
}) {
  return (
    <section className="mx-auto w-full max-w-md section-fade">
      <div className="mb-6 text-center">
        <p className="eyebrow mb-2 text-[10px] text-[var(--accent)]">Welcome Back</p>
        <h1 className="font-display text-5xl leading-none tracking-[-0.06em] text-[var(--foreground)] sm:text-6xl">
          Loomic
        </h1>
      </div>

      <div className="surface-panel rounded-[1.5rem] p-6 sm:p-7">
        <div className="relative z-10 grid grid-cols-2 rounded-full border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] p-1">
          <button
            className={`relative z-10 rounded-full px-4 py-3 text-sm touch-manipulation ${
              view === "login"
                ? "bg-[rgba(241,205,146,0.14)] text-[var(--foreground)]"
                : "text-[var(--muted)]"
            }`}
            onClick={onSwitchToLogin}
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
            onClick={onSwitchToCreateAccount}
            type="button"
          >
            Create Account
          </button>
        </div>

        {children}
      </div>
    </section>
  );
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
  const usernameIsValid = trimmedUsername.length > 0;
  const emailIsValid = trimmedEmail.length > 0 && isValidEmail(trimmedEmail);
  const passwordIsValid = password.length >= MIN_PASSWORD_LENGTH;
  const createAccountIssues: string[] = [];

  if (!usernameIsValid) {
    createAccountIssues.push("Add a username.");
  }

  if (trimmedEmail.length === 0) {
    createAccountIssues.push("Add an email address.");
  } else if (!emailIsValid) {
    createAccountIssues.push("Enter a valid email address.");
  }

  if (password.length === 0) {
    createAccountIssues.push("Create a password.");
  } else if (!passwordIsValid) {
    createAccountIssues.push(
      `Use at least ${MIN_PASSWORD_LENGTH} characters (${MIN_PASSWORD_LENGTH - password.length} more to go).`,
    );
  }

  const canCreateAccount = createAccountIssues.length === 0;
  const usernameRequirement = usernameIsValid ? null : "Username is required.";
  const emailRequirement =
    trimmedEmail.length === 0
      ? "Enter an email address."
      : emailIsValid
        ? null
        : "Enter a valid email address, like name@example.com.";
  const passwordRequirement =
    password.length === 0
      ? `Password must be at least ${MIN_PASSWORD_LENGTH} characters.`
      : passwordIsValid
        ? null
        : `Password must be at least ${MIN_PASSWORD_LENGTH} characters.`;

  useEffect(() => {
    const session = readStoredSession();

    if (session) {
      router.replace("/chat");
      return;
    }

    setReady(true);
  }, [router]);

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

  if (!ready) {
    return (
      <section className="mx-auto w-full max-w-md section-fade">
        <div className="surface-panel rounded-[1.5rem] px-6 py-10 text-center sm:px-7">
          <p className="eyebrow text-[10px] text-[var(--accent)]">Loomic</p>
          <p className="mt-4 text-sm text-[var(--muted)]">Checking your session...</p>
        </div>
      </section>
    );
  }

  return (
    <AuthPanel
      onSwitchToCreateAccount={switchToCreateAccount}
      onSwitchToLogin={switchToLogin}
      view={view}
    >
      {view === "login" ? (
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
        </form>
      ) : (
        <form className="mt-6 space-y-4" onSubmit={handleSubmit}>
          <div className="space-y-2">
            <div className="flex items-center justify-between px-1">
              <span className="text-sm text-[var(--foreground)]">Username</span>
              <FieldRequirementNotice message={usernameRequirement} />
            </div>
            <input
              autoComplete="username"
              className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
              onChange={(event) => setUsername(event.target.value)}
              placeholder="Username"
              required
              value={username}
            />
          </div>
          <div className="space-y-2">
            <div className="flex items-center justify-between px-1">
              <span className="text-sm text-[var(--foreground)]">Email</span>
              <FieldRequirementNotice message={emailRequirement} />
            </div>
            <input
              autoComplete="email"
              className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
              onChange={(event) => setEmail(event.target.value)}
              placeholder="Email"
              required
              type="email"
              value={email}
            />
          </div>
          <div className="space-y-2">
            <div className="flex items-center justify-between px-1">
              <span className="text-sm text-[var(--foreground)]">Password</span>
              <FieldRequirementNotice message={passwordRequirement} />
            </div>
            <input
              autoComplete="new-password"
              className="w-full rounded-[1rem] border border-[rgba(255,255,255,0.08)] bg-[rgba(255,255,255,0.03)] px-4 py-3 text-[var(--foreground)]"
              minLength={MIN_PASSWORD_LENGTH}
              onChange={(event) => setPassword(event.target.value)}
              placeholder="Password"
              required
              type="password"
              value={password}
            />
          </div>
          {error ? (
            <div className="rounded-[1rem] border border-[rgba(245,143,124,0.28)] bg-[rgba(245,143,124,0.08)] px-4 py-3 text-sm text-[var(--foreground)]">
              {error}
            </div>
          ) : null}
          <button
            className="inline-flex min-h-11 w-full items-center justify-center rounded-full bg-[rgba(241,205,146,0.14)] px-5 text-sm text-[var(--foreground)] hover:bg-[rgba(241,205,146,0.22)] disabled:cursor-not-allowed disabled:opacity-60"
            disabled={pending || !canCreateAccount}
            type="submit"
          >
            {pending ? "Creating Account..." : "Create Account"}
          </button>
        </form>
      )}
    </AuthPanel>
  );
}
