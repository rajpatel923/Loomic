#pragma once

#include <string>

namespace Loomic {

class HttpServer;

/// Register GET /openapi.json and GET /docs on the server.
/// spec_json is the full OpenAPI 3.0.3 document read from disk at startup
/// and cached in memory for the server's lifetime.
void register_docs_routes(HttpServer& server, std::string spec_json);

} // namespace Loomic
