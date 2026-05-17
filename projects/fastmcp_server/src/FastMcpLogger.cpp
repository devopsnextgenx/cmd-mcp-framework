#include "FastMcpLogger.hpp"
#include "FastMcpStringUtils.hpp"

#include <iostream>
#include <mutex>

namespace fastmcp
{

std::mutex gLogMutex;

void logDiag(const std::string& area, const std::string& message)
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    std::cout << "[" << FastMcpStringUtils::nowUtcIso8601() << "] ["
              << area << "] " << message << '\n';
}

void logRequestContext(const std::string& area,
                       const mcp::jsonrpc::RequestContext& request_context,
                       const std::string& method_or_target)
{
    logDiag(area,
        method_or_target +
        " session=" + FastMcpStringUtils::safeSessionId(request_context.sessionId) +
        " protocol=" + request_context.protocolVersion);
}

} // namespace fastmcp