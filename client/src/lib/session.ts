import { decodeJwtSubject } from "@/lib/jwt";

export type StoredSession = {
  access_token: string;
  refresh_token: string;
  token_type: string;
  username: string;
  persisted: boolean;
  client_session_id: string;
  user_id: string | null;
};

const SESSION_STORAGE_KEY = "loomic.session";

function buildClientSessionId() {
  if (typeof crypto !== "undefined" && typeof crypto.randomUUID === "function") {
    return crypto.randomUUID();
  }

  return `session-${Date.now()}-${Math.random().toString(16).slice(2, 10)}`;
}

function normalizeSession(
  session: Omit<StoredSession, "client_session_id" | "user_id"> & {
    client_session_id?: string;
    user_id?: string | null;
  },
): StoredSession {
  return {
    ...session,
    client_session_id: session.client_session_id || buildClientSessionId(),
    user_id: session.user_id ?? decodeJwtSubject(session.access_token),
  };
}

export function readStoredSession() {
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
    const parsed = normalizeSession(
      JSON.parse(raw) as Omit<StoredSession, "client_session_id" | "user_id"> & {
        client_session_id?: string;
        user_id?: string | null;
      },
    );
    const serialized = JSON.stringify(parsed);

    if (fromLocal === raw && raw !== serialized) {
      window.localStorage.setItem(SESSION_STORAGE_KEY, serialized);
    }

    if (fromSession === raw && raw !== serialized) {
      window.sessionStorage.setItem(SESSION_STORAGE_KEY, serialized);
    }

    return parsed;
  } catch {
    window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
    window.localStorage.removeItem(SESSION_STORAGE_KEY);
    return null;
  }
}

export function saveStoredSession(
  session: Omit<StoredSession, "client_session_id" | "user_id"> & {
    client_session_id?: string;
    user_id?: string | null;
  },
  persist: boolean,
) {
  const serialized = JSON.stringify(normalizeSession(session));

  if (persist) {
    window.localStorage.setItem(SESSION_STORAGE_KEY, serialized);
    window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
    return;
  }

  window.sessionStorage.setItem(SESSION_STORAGE_KEY, serialized);
  window.localStorage.removeItem(SESSION_STORAGE_KEY);
}

export function clearStoredSession() {
  if (typeof window === "undefined") {
    return;
  }

  window.localStorage.removeItem(SESSION_STORAGE_KEY);
  window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
}
