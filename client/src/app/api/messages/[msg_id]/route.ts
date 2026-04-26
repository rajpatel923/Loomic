import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function DELETE(
  request: NextRequest,
  context: RouteContext<"/api/messages/[msg_id]">,
) {
  const { msg_id } = await context.params;
  const upstreamUrl = new URL(
    `${getApiBaseUrl()}/messages/${encodeURIComponent(msg_id)}`,
  );

  for (const [key, value] of request.nextUrl.searchParams.entries()) {
    upstreamUrl.searchParams.set(key, value);
  }

  try {
    const response = await fetch(upstreamUrl, {
      method: "DELETE",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/messages/[msg_id]",
      upstreamUrl: upstreamUrl.toString(),
    });
  } catch (error) {
    logProxyNetworkError(
      "/api/messages/[msg_id]",
      upstreamUrl.toString(),
      error,
    );

    return buildNetworkErrorResponse("Unable to delete the Loomic message.");
  }
}
