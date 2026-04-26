import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function GET(
  request: NextRequest,
  context: RouteContext<"/api/groups/[id]">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/groups/${encodeURIComponent(id)}`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "GET",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/groups/[id]",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/groups/[id]", upstreamUrl, error);

    return buildNetworkErrorResponse("Unable to load the Loomic group.");
  }
}

export async function PATCH(
  request: NextRequest,
  context: RouteContext<"/api/groups/[id]">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/groups/${encodeURIComponent(id)}`;

  try {
    const payload = await request.text();
    const response = await fetch(upstreamUrl, {
      method: "PATCH",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
        "content-type":
          request.headers.get("content-type") ?? "application/json",
      },
      body: payload,
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/groups/[id]",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/groups/[id]", upstreamUrl, error);

    return buildNetworkErrorResponse("Unable to update the Loomic group.");
  }
}
