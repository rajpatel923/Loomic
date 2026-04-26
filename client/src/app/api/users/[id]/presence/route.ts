import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function GET(
  request: NextRequest,
  context: RouteContext<"/api/users/[id]/presence">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/users/${encodeURIComponent(id)}/presence`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "GET",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/users/[id]/presence",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/users/[id]/presence", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to load the Loomic presence service.",
    );
  }
}
