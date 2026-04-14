#include "LoomicServer/http/DocsHandler.hpp"
#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>

#include <string>
#include <utility>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

// ── Swagger UI HTML ────────────────────────────────────────────────────────
//
// Loaded from unpkg CDN (swagger-ui-dist@5) — no local assets required.
// The spec is fetched from /openapi.json on the same server origin, so
// no CORS issues arise when "Try it out" fires real requests.

static const std::string kSwaggerHtml = R"html(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Loomic API &mdash; Swagger UI</title>
  <link rel="stylesheet"
        href="https://unpkg.com/swagger-ui-dist@5/swagger-ui.css" />
  <style>
    body  { margin: 0; background: #fafafa; }
    #swagger-ui .topbar { display: none; }
  </style>
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
  <script src="https://unpkg.com/swagger-ui-dist@5/swagger-ui-standalone-preset.js"></script>
  <script>
    window.onload = function () {
      window.ui = SwaggerUIBundle({
        url: "/openapi.json",
        dom_id: "#swagger-ui",
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        layout: "StandaloneLayout",
        deepLinking: true,
        displayRequestDuration: true,
        persistAuthorization: true,
        tryItOutEnabled: true
      });
    };
  </script>
</body>
</html>
)html";

// ── Route registration ─────────────────────────────────────────────────────

void register_docs_routes(HttpServer& server, std::string spec_json)
{
    // ── GET /openapi.json ──────────────────────────────────────────────────
    // Serves the cached spec with CORS headers so external tooling (e.g. a
    // locally-run Swagger UI or Redoc) can fetch the spec freely.
    server.add_route(http::verb::get, "/openapi.json",
        [spec = std::move(spec_json)](Request /*req*/) -> net::awaitable<Response> {
            Response res;
            res.result(http::status::ok);
            res.set(http::field::content_type,                "application/json");
            res.set(http::field::access_control_allow_origin, "*");
            res.set("Access-Control-Allow-Methods",           "GET, OPTIONS");
            res.set("Access-Control-Allow-Headers",           "Content-Type, Authorization");
            res.body() = spec;
            co_return res;
        });

    // ── GET /docs ──────────────────────────────────────────────────────────
    // Serves the Swagger UI HTML page. The page self-fetches /openapi.json
    // from the same origin, so the spec and UI are always in sync.
    server.add_route(http::verb::get, "/docs",
        [](Request /*req*/) -> net::awaitable<Response> {
            Response res;
            res.result(http::status::ok);
            res.set(http::field::content_type, "text/html; charset=utf-8");
            res.body() = kSwaggerHtml;
            co_return res;
        });

    LOG_INFO("Docs routes registered: GET /openapi.json  GET /docs");
}

} // namespace Loomic
