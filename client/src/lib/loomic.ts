const DEFAULT_API_BASE_URL = "http://127.0.0.1:7777";

function normalizeBaseUrl(value: string) {
  return value.endsWith("/") ? value.slice(0, -1) : value;
}

export function getApiBaseUrl() {
  return normalizeBaseUrl(
    process.env.LOOMIC_API_BASE_URL ?? DEFAULT_API_BASE_URL,
  );
}

export async function buildProxyResponse(response: Response) {
  const bodyText = await response.text();
  const contentType =
    response.headers.get("content-type") ?? "application/json; charset=utf-8";

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
