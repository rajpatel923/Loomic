import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
  logProxyNetworkError,
} from "@/lib/loomic";

export async function DELETE(
  request: NextRequest,
  context: RouteContext<"/api/groups/[id]/members/[uid]">,
) {
  const { id, uid } = await context.params;
  const upstreamUrl = `${getApiBaseUrl()}/groups/${encodeURIComponent(id)}/members/${encodeURIComponent(uid)}`;

  try {
    const response = await fetch(upstreamUrl, {
      method: "DELETE",
      headers: {
        authorization: request.headers.get("authorization") ?? "",
      },
      cache: "no-store",
    });

    return buildProxyResponse(response, {
      route: "/api/groups/[id]/members/[uid]",
      upstreamUrl,
    });
  } catch (error) {
    logProxyNetworkError("/api/groups/[id]/members/[uid]", upstreamUrl, error);

    return buildNetworkErrorResponse(
      "Unable to remove the Loomic group member.",
    );
  }
}
