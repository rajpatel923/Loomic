import type { NextRequest } from "next/server";

import { getChatGatewayManager } from "@/lib/server/chat-gateway";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const encoder = new TextEncoder();

function toSseMessage(event: string, payload: unknown) {
  return encoder.encode(`event: ${event}\ndata: ${JSON.stringify(payload)}\n\n`);
}

export async function GET(request: NextRequest) {
  const clientSessionId = request.nextUrl.searchParams
    .get("clientSessionId")
    ?.trim();

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

  const manager = getChatGatewayManager();
  const connection = manager.getConnection(clientSessionId);

  const stream = new ReadableStream<Uint8Array>({
    start(controller) {
      controller.enqueue(
        toSseMessage("snapshot", manager.buildSnapshot(clientSessionId)),
      );

      const unsubscribe = connection.subscribe((event) => {
        controller.enqueue(toSseMessage(event.event, event.payload));
      });

      const keepAlive = setInterval(() => {
        controller.enqueue(encoder.encode(`: ping ${Date.now()}\n\n`));
      }, 15_000);

      const cleanup = () => {
        clearInterval(keepAlive);
        unsubscribe();
        controller.close();
      };

      request.signal.addEventListener("abort", cleanup, {
        once: true,
      });
    },
  });

  return new Response(stream, {
    headers: {
      "cache-control": "no-cache, no-transform",
      connection: "keep-alive",
      "content-type": "text/event-stream; charset=utf-8",
    },
  });
}
