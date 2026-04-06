import type { NextRequest } from "next/server";

import {
  buildNetworkErrorResponse,
  buildProxyResponse,
  getApiBaseUrl,
} from "@/lib/loomic";

export async function POST(request: NextRequest) {
  try {
    const payload = await request.text();
    const response = await fetch(`${getApiBaseUrl()}/auth/login`, {
      method: "POST",
      headers: {
        "content-type":
          request.headers.get("content-type") ?? "application/json",
      },
      body: payload,
      cache: "no-store",
    });

    return buildProxyResponse(response);
  } catch {
    return buildNetworkErrorResponse(
      "Unable to reach the Loomic sign-in service.",
    );
  }
}
