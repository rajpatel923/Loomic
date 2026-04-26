import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function POST(
  request: NextRequest,
  context: RouteContext<"/api/conversations/[id]/read">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/conversations/${encodeURIComponent(id)}/read`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "POST",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/conversations/[id]/read",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/conversations/[id]/read", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to reach the Loomic read-state service.",
    );
  }
}
