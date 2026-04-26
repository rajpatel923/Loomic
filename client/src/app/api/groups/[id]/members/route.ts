import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function POST(
  request: NextRequest,
  context: RouteContext<"/api/groups/[id]/members">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/groups/${encodeURIComponent(id)}/members`;

  try {
    const payload = await request.text();
    const response = await fetch(upstreamUrl, {
      method: "POST",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
        "content-type":
          request.headers.get("content-type") ?? "application/json",
      },
      body: payload,
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/groups/[id]/members",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/groups/[id]/members", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to update the Loomic group members.",
    );
  }
}
