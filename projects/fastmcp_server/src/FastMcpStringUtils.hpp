#pragma once

#include <optional>
#include <string>
#include <map>
#include "cmdsdk/CommandMetadata.hpp"

namespace fastmcp
{

enum class ProtocolMode { MCP_ONLY, REST_ONLY, ALL };

using PluginInfo = std::map<std::string, cmdsdk::SubCmdTypeMetadata>;

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
    static std::string buildPluginsMarkdown(const std::map<std::string, PluginInfo>& plugins);
    static std::string buildPluginDetailsMarkdown(const std::string& pname, const PluginInfo& pi);
};

} // namespace fastmcp
