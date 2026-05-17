// RestImplementation.hpp
// Auxiliary httplib REST + Swagger server (non-MCP routes).
// Owns: route registration, CORS, homepage, swagger, openapi, proxy.
#pragma once

#include <string>
#include <httplib.h>
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/OpenApiAggregator.hpp"

namespace fastmcp
{

// ── CORS ──────────────────────────────────────────────────────────────────
void addCorsHeaders(httplib::Response& res);

// ── Homepage HTML ─────────────────────────────────────────────────────────
std::string defaultHtmlPage(int mcp_port, int rest_port);

// ── Register every non-MCP route on `server` ─────────────────────────────
// protocol_mode_is_mcp_only: when true, the POST /api/:command route is skipped.
void registerRestRoutes(httplib::Server& server,
                        cmdsdk::CommandRegistry& registry,
                        cmdsdk::OpenApiAggregator& api_aggregator,
                        int mcp_port,
                        int rest_port,
                        bool protocol_mode_is_mcp_only);

} // namespace fastmcp