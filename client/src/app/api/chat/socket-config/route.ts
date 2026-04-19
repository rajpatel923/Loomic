import { getWebSocketUrl } from "@/lib/loomic";

export async function GET() {
  return Response.json(
    {
      url: getWebSocketUrl(),
    },
    {
      headers: {
        "cache-control": "no-store",
      },
    },
  );
}
