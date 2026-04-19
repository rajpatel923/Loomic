import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function GET(request: NextRequest) {
  const upstreamUrl = `${getApiBaseUrl()}/conversations`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "GET",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/conversations",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/conversations", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to reach the Loomic conversations service.",
    );
  }
}

export async function POST(request: NextRequest) {
  const upstreamUrl = `${getApiBaseUrl()}/conversations`;

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
      route: "/api/conversations",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/conversations", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to create the Loomic conversation.",
    );
  }
}
