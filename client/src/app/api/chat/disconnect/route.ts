import type { NextRequest } from "next/server";

import { getChatGatewayManager } from "@/lib/server/chat-gateway";

export const runtime = "nodejs";

type DisconnectBody = {
  client_session_id?: string;
};

export async function POST(request: NextRequest) {
  const body = (await request.json().catch(() => null)) as DisconnectBody | null;
  const clientSessionId = body?.client_session_id?.trim();

  if (!clientSessionId) {
    return Response.json(
      {
        error: "Missing client session id.",
      },
      {
        status: 400,
      },
    );
  }

  getChatGatewayManager().destroyConnection(clientSessionId);

  return Response.json(
    {
      ok: true,
    },
    {
      headers: {
        "cache-control": "no-store",
      },
    },
  );
}
