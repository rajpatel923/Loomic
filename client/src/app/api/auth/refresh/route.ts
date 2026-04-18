import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function POST(request: NextRequest) {
  const upstreamUrl = `${getApiBaseUrl()}/auth/refresh`;

  try {
    const payload = await request.text();
    const response = await fetch(upstreamUrl, {
      method: "POST",
      headers: {
        "content-type":
          request.headers.get("content-type") ?? "application/json",
      },
      body: payload,
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/auth/refresh",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/auth/refresh", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to reach the Loomic refresh-token service.",
    );
  }
}
