import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function GET(
  request: NextRequest,
  context: RouteContext<"/api/conversations/[id]/messages">,
) {
  const { id } = await context.params;
  const upstreamUrl = new URL(
    `${getApiBaseUrl()}/conversations/${encodeURIComponent(id)}/messages`,
  );

  for (const [key, value] of request.nextUrl.searchParams.entries()) {
    upstreamUrl.searchParams.set(key, value);
  }

  try {
    const response = await fetch(upstreamUrl, {
      method: "GET",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/conversations/[id]/messages",
      upstreamUrl: upstreamUrl.toString(),
    });
  } catch (error) {
    logProxyNetworkError(
      "/api/conversations/[id]/messages",
      upstreamUrl.toString(),
      error,
    );

    return buildNetworkErrorResponse(
      "Unable to reach the Loomic message history service.",
    );
  }
}
