import type { NextRequest } from "next/server";

import { getChatGatewayManager } from "@/lib/server/chat-gateway";

export const runtime = "nodejs";

type ConnectBody = {
  access_token?: string;
  client_session_id?: string;
};

export async function POST(request: NextRequest) {
  const body = (await request.json().catch(() => null)) as ConnectBody | null;
  const accessToken = body?.access_token?.trim();
  const clientSessionId = body?.client_session_id?.trim();

  if (!accessToken || !clientSessionId) {
    return Response.json(
      {
        error: "Missing access token or client session id.",
      },
      {
        status: 400,
      },
    );
  }

  try {
    const manager = getChatGatewayManager();
    const connection = manager.getConnection(clientSessionId);
    await connection.ensureConnected(accessToken);

    return Response.json(
      {
        status: connection.getSnapshot(),
      },
      {
        headers: {
          "cache-control": "no-store",
        },
      },
    );
  } catch (error) {
    return Response.json(
      {
        error:
          error instanceof Error
            ? error.message
            : "Unable to open the Loomic messaging bridge.",
      },
      {
        status: 502,
      },
    );
  }
}
