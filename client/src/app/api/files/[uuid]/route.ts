import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export const runtime = "nodejs";

export async function GET(
  request: NextRequest,
  context: RouteContext<"/api/files/[uuid]">,
) {
  const { uuid } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/files/${encodeURIComponent(uuid)}`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "GET",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    if (!response.ok) {
      const text = await response.text();
      return new Response(text || null, {
        status: response.status,
        headers: {
          "cache-control": "no-store",
          "content-type":
            response.headers.get("content-type") ??
            "application/json; charset=utf-8",
        },
      });
    }

    return new Response(response.body, {
      status: response.status,
      headers: {
        "cache-control": "no-store",
        "content-type":
          response.headers.get("content-type") ?? "application/octet-stream",
      },
    });
  } catch (error) {
    logProxyNetworkError("/api/files/[uuid]", upstreamUrl, error);

    return buildNetworkErrorResponse("Unable to load the Loomic file.");
  }
}
