#pragma once
#include <mutex>
#include <string>
#include <mcp/jsonrpc/request_context.hpp>  // adjust include path to match your SDK layout

namespace fastmcp
{

// Defined in FastMcpLogger.cpp
extern std::mutex gLogMutex;

void logDiag(const std::string& area, const std::string& message);

void logRequestContext(const std::string& area,
                       const mcp::jsonrpc::RequestContext& request_context,
                       const std::string& method_or_target);

} // namespace fastmcp