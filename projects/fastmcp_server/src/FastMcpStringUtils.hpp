#pragma once

#include <optional>
#include <string>

namespace fastmcp
{

enum class ProtocolMode { MCP_ONLY, REST_ONLY, ALL };

class FastMcpStringUtils
{
public:
    static bool beginsWith(const std::string& value, const std::string& prefix);
    static bool endsWith(const std::string& value, const std::string& suffix);
    static std::string toUpperAscii(std::string value);
    static std::string toLowerAscii(std::string value);
    static std::string sanitizeToolName(std::string name);
    static std::string nowUtcIso8601();
    static std::string safeSessionId(const std::optional<std::string>& session_id);
    static bool uriToMcpAppsPath(const std::string& uri, std::string& path);
    static std::string toMcpAppUri(const std::string& uri);
    static std::string protocolModeToString(ProtocolMode mode);
};

} // namespace fastmcp
