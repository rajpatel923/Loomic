export type StoredSession = {
  access_token: string;
  refresh_token: string;
  token_type: string;
  username: string;
  persisted: boolean;
};

const SESSION_STORAGE_KEY = "loomic.session";

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
    return JSON.parse(raw) as StoredSession;
  } catch {
    window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
    window.localStorage.removeItem(SESSION_STORAGE_KEY);
    return null;
  }
}

export function saveStoredSession(session: StoredSession, persist: boolean) {
  const serialized = JSON.stringify(session);

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
