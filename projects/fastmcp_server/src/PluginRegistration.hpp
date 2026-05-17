// PluginRegistration.hpp
// Plugin discovery, loading, OpenAPI aggregation, and MCP tool/resource
// registration helpers.  All state that was previously in anonymous-namespace
// helpers in main.cpp now lives here.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <mcp/server/all.hpp>

#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/OpenApiAggregator.hpp"
#include "cmdsdk/PluginMetadata.hpp"

namespace fastmcp
{

    using json = nlohmann::json;

    // ── Tool name de-duplication ───────────────────────────────────────────────
    struct ToolRegistrationState
    {
        std::set<std::string>                    used_tool_names;
        std::map<std::string, int>               tool_name_suffixes;
        std::map<std::string, std::vector<std::string>> command_tool_names;
    };

    std::string allocateUniqueToolName(const std::string& base_name,
                                    ToolRegistrationState& state);

    // ── Input schema builder ───────────────────────────────────────────────────
    json buildInputSchema(const cmdsdk::CommandMetadata& cmd_meta,
                        const std::optional<std::vector<std::string>>& fixed_subtype);

    // ── Low-level tool registration ───────────────────────────────────────────
    bool registerTool(const cmdsdk::CommandRegistry& registry,
                    mcp::server::Server& server,
                    const std::string& tool_name,
                    const std::string& title,
                    const std::string& description,
                    const json& input_schema,
                    const std::string& original_cmd_name,
                    const std::function<json(const json&)>& handler);

    // ── Command → plain tool ──────────────────────────────────────────────────
    bool registerCmdAsTool(const cmdsdk::CommandRegistry& registry,
                        mcp::server::Server& server,
                        const cmdsdk::CommandMetadata& cmd_meta,
                        const std::function<json(const json&)>& handler,
                        ToolRegistrationState& registration_state);

    // ── Command → UI-backed app tool ──────────────────────────────────────────
    bool registerToolWithUI(mcp::server::Server& server,
                            const std::string& toolName,
                            const std::string& title,
                            const std::string& description,
                            const std::string& resource_uri,
                            const json& input_schema,
                            const std::function<mcp::server::CallToolResult(const json&)>& handler);

    bool registerAppToolWithUI(const cmdsdk::CommandRegistry& registry,
                            mcp::server::Server& server,
                            const cmdsdk::CommandMetadata& cmd_meta,
                            ToolRegistrationState& registration_state);

    // ── Plugin file discovery ─────────────────────────────────────────────────
    std::vector<std::filesystem::path> getAllPluginsInLib(const char* argv0);

    // ── Plugin name/path helpers ──────────────────────────────────────────────
    std::string canonicalResourceName(const std::string& plugin_name);
    std::string resolvePluginName(const cmdsdk::CommandMetadata& m);
    std::string openApiPathForCommand(const cmdsdk::CommandMetadata& m);

    // ── OpenAPI spec helpers ──────────────────────────────────────────────────
    bool mergeMissingSubtypeEnumsIntoSpec(json& spec,
                                        const cmdsdk::CommandMetadata& meta);

    // ── Per-plugin subtype map (used by resource registration) ────────────────
    using PluginInfo = std::map<std::string, cmdsdk::SubCmdTypeMetadata>;

    std::map<std::string, PluginInfo> buildPluginRegistry(
        const cmdsdk::CommandRegistry& reg);

    const PluginInfo* findPluginInfo(const std::map<std::string, PluginInfo>& plugins,
                                    const std::string& requested,
                                    std::string& resolved);

    // ── External mcp-apps resource discovery & reading ────────────────────────
    struct ExternalResourceInfo
    {
        std::string uri, name, description, mime_type;
        json        meta;
    };

    std::vector<ExternalResourceInfo> fetchExternalAppResources();

    bool readMcpAppResource(const std::string& uri,
                            std::string& canonical_uri,
                            std::string& mime_type,
                            std::string& content,
                            std::string& error);

    // ── OpenAPI aggregation pass (called once after all plugins load) ─────────
    // Iterates registry, fills api_aggregator, prints the command table.
    void buildOpenApiAggregator(const cmdsdk::CommandRegistry& registry,
                                cmdsdk::OpenApiAggregator& api_aggregator);

} // namespace fastmcp