import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function POST(request: NextRequest) {
  const upstreamUrl = `${getApiBaseUrl()}/groups`;

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
      route: "/api/groups",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/groups", upstreamUrl, error);

    return buildNetworkErrorResponse("Unable to create the Loomic group.");
  }
}
