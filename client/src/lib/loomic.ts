const DEFAULT_API_BASE_URL = "http://127.0.0.1:8080";

function normalizeBaseUrl(value: string) {
  return value.endsWith("/") ? value.slice(0, -1) : value;
}

function formatTerminalError(error: unknown) {
  if (error instanceof Error) {
    return {
      name: error.name,
      message: error.message,
      stack: error.stack,
    };
  }

  return error;
}

export function getApiBaseUrl() {
  return normalizeBaseUrl(
    process.env.LOOMIC_API_BASE_URL ?? DEFAULT_API_BASE_URL,
  );
}

function normalizeWebSocketProtocol(protocol: string) {
  if (protocol === "https:" || protocol === "wss:") {
    return "wss:";
  }

  return "ws:";
}

export function getWebSocketUrl(protocolHint?: string) {
  const explicitWebSocketUrl = process.env.LOOMIC_WS_URL;
  const url = new URL(explicitWebSocketUrl ?? getApiBaseUrl());
  if (!explicitWebSocketUrl) {
    url.protocol = normalizeWebSocketProtocol(protocolHint ?? url.protocol);
  }
  url.pathname = "/ws";
  url.search = "";
  url.hash = "";
  return url.toString();
}

export function getTcpHost() {
  if (process.env.LOOMIC_TCP_HOST) {
    return process.env.LOOMIC_TCP_HOST;
  }

  return new URL(getApiBaseUrl()).hostname;
}

export function getChatPort() {
  const value = process.env.LOOMIC_TCP_PORT;

  if (!value) {
    return 9000;
  }

  const parsed = Number.parseInt(value, 10);
  return Number.isFinite(parsed) ? parsed : 9000;
}

export function getChatServerName(host: string) {
  return process.env.LOOMIC_TCP_SERVERNAME ?? host;
}

export function shouldVerifyChatTls() {
  const value = process.env.LOOMIC_TCP_VERIFY_TLS;

  if (!value) {
    return false;
  }

  return value === "true" || value === "1";
}

export async function buildProxyResponse(
  response: Response,
  context?: {
    route: string;
    upstreamUrl: string;
  },
) {
  const bodyText = await response.text();
  const contentType =
    response.headers.get("content-type") ?? "application/json; charset=utf-8";
  const isHtmlError =
    !response.ok &&
    (contentType.toLowerCase().includes("text/html") ||
      bodyText.trimStart().startsWith("<html"));

  if (!response.ok && context) {
    console.error(
      `[Loomic proxy] ${context.route} -> ${context.upstreamUrl} failed with ${response.status} ${response.statusText}`,
    );

    if (bodyText.length > 0) {
      console.error(
        isHtmlError
          ? `[Loomic proxy] Upstream returned an HTML error page (${bodyText.length} chars).`
          : `[Loomic proxy] Upstream response body: ${bodyText}`,
      );
    }
  }

  const noBodyStatus = response.status === 204 || response.status === 205 || response.status === 304;

  if (isHtmlError) {
    return Response.json(
      { error: `Loomic backend returned ${response.status} ${response.statusText}.` },
      {
        status: response.status,
        headers: {
          "cache-control": "no-store",
        },
      },
    );
  }

  return new Response(noBodyStatus ? null : bodyText, {
    status: response.status,
    headers: {
      "content-type": contentType,
      "cache-control": "no-store",
    },
  });
}

export function buildNetworkErrorResponse(message: string) {
  return Response.json(
    { error: message },
    {
      status: 502,
      headers: {
        "cache-control": "no-store",
      },
    },
  );
}

export function logProxyNetworkError(
  route: string,
  upstreamUrl: string,
  error: unknown,
) {
  console.error(`[Loomic proxy] ${route} -> ${upstreamUrl} threw before a response`);
  console.error(formatTerminalError(error));
}
