import type { NextRequest } from "next/server";

import { getChatGatewayManager } from "@/lib/server/chat-gateway";

export const runtime = "nodejs";

type SendBody = {
  access_token?: string;
  client_session_id?: string;
  recipient_id?: string;
  content?: string;
};

function isSnowflakeLike(value: string) {
  return /^\d+$/.test(value);
}

export async function POST(request: NextRequest) {
  const body = (await request.json().catch(() => null)) as SendBody | null;
  const accessToken = body?.access_token?.trim();
  const clientSessionId = body?.client_session_id?.trim();
  const recipientId = body?.recipient_id?.trim();
  const content = body?.content?.trim();

  if (!accessToken || !clientSessionId || !recipientId || !content) {
    return Response.json(
      {
        error: "Missing access token, session id, recipient id, or content.",
      },
      {
        status: 400,
      },
    );
  }

  if (!isSnowflakeLike(recipientId)) {
    return Response.json(
      {
        error: "Recipient ids must be numeric Snowflake ids.",
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
    const message = await connection.sendMessage(recipientId, content);

    return Response.json(
      {
        message,
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
            : "Unable to send the Loomic message.",
      },
      {
        status: 502,
      },
    );
  }
}
