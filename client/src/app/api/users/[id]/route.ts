import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function GET(
  request: NextRequest,
  context: RouteContext<"/api/users/[id]">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/users/${encodeURIComponent(id)}`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "GET",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/users/[id]",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/users/[id]", upstreamUrl, error);

    return buildNetworkErrorResponse("Unable to load the Loomic user profile.");
  }
}

export async function PATCH(
  request: NextRequest,
  context: RouteContext<"/api/users/[id]">,
) {
  const { id } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/users/${encodeURIComponent(id)}`;

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
      route: "/api/users/[id]",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/users/[id]", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to update the Loomic user profile.",
    );
  }
}
