import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function GET(request: NextRequest) {
  const upstreamUrl = new URL(`${getApiBaseUrl()}/users/search`);

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
      route: "/api/users/search",
      upstreamUrl: upstreamUrl.toString(),
    });
  } catch (error) {
    logProxyNetworkError("/api/users/search", upstreamUrl.toString(), error);

    return buildNetworkErrorResponse(
      "Unable to reach the Loomic user search service.",
    );
  }
}
