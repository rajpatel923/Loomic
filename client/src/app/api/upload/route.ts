import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export const runtime = "nodejs";

export async function POST(request: NextRequest) {
  const upstreamUrl = `${getApiBaseUrl()}/upload`;

  try {
    const payload = await request.arrayBuffer();
    const response = await fetch(upstreamUrl, {
      method: "POST",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
        "content-type":
          request.headers.get("content-type") ?? "multipart/form-data",
      },
      body: payload,
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/upload",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/upload", upstreamUrl, error);

    return buildNetworkErrorResponse("Unable to upload the Loomic file.");
  }
}
