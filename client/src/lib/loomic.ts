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

  if (!response.ok && context) {
    console.error(
      `[Loomic proxy] ${context.route} -> ${context.upstreamUrl} failed with ${response.status} ${response.statusText}`,
    );

    if (bodyText.length > 0) {
      console.error(`[Loomic proxy] Upstream response body: ${bodyText}`);
    }
  }

  return new Response(bodyText, {
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
