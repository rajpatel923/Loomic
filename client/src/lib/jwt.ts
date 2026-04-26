type JwtPayload = {
  sub?: string;
  exp?: number;
};

function decodeBase64Url(segment: string) {
  const normalized = segment.replace(/-/g, "+").replace(/_/g, "/");
  const padLength = (4 - (normalized.length % 4)) % 4;
  const padded = `${normalized}${"=".repeat(padLength)}`;

  if (typeof atob === "function") {
    return atob(padded);
  }

  return Buffer.from(padded, "base64").toString("utf8");
}

export function decodeJwtPayload(token: string): JwtPayload | null {
  const [, payload] = token.split(".");

  if (!payload) {
    return null;
  }

  try {
    return JSON.parse(decodeBase64Url(payload)) as JwtPayload;
  } catch {
    return null;
  }
}

export function decodeJwtSubject(token: string) {
  return decodeJwtPayload(token)?.sub ?? null;
}

export function decodeJwtExpiry(token: string) {
  return decodeJwtPayload(token)?.exp ?? null;
}
