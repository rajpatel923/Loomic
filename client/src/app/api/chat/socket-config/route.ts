import { getWebSocketUrl } from "@/lib/loomic";

export async function GET(request: Request) {
  const forwardedProto = request.headers.get("x-forwarded-proto");
  const requestProtocol = forwardedProto
    ? `${forwardedProto.split(",")[0]?.trim() ?? "https"}:`
    : new URL(request.url).protocol;

  return Response.json(
    {
      url: getWebSocketUrl(requestProtocol),
    },
    {
      headers: {
        "cache-control": "no-store",
      },
    },
  );
}
