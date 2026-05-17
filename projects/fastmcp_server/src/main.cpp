// ---------------------------------------------------------------------------
// fastmcp_server/src/main.cpp
//
// MCP protocol layer  → cpp-mcp-sdk (itcv-GmbH/cpp-mcp-sdk, C++17)
//   Key APIs:
//     • mcp::server::Server::create(configuration)
//     • server->registerTool(definition, handler)
//     • server->registerResource(definition, handler)
//     • mcp::server::StreamableHttpServerRunner for /mcp transport
//
// Auxiliary REST layer and plugin registration are in their own TUs:
//   RestImplementation.cpp  — httplib routes, Swagger, CORS, homepage
//   PluginRegistration.cpp  — plugin discovery, tool/resource registration
//
// Port layout:
//   MCP  → port N   (e.g. 5432) — cpp-mcp-sdk streamable HTTP runner
//   REST → port N+1 (e.g. 5433) — httplib on a detached thread
// ---------------------------------------------------------------------------

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ── cpp-mcp-sdk ───────────────────────────────────────────────────────────
#include <mcp/lifecycle/session/implementation.hpp>
#include <mcp/lifecycle/session/resources_capability.hpp>
#include <mcp/lifecycle/session/server_capabilities.hpp>
#include <mcp/lifecycle/session/tools_capability.hpp>
#include <mcp/server/all.hpp>

// ── Auxiliary HTTP ────────────────────────────────────────────────────────
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "FastMcpStringUtils.hpp"
#include "PluginLoader.hpp"
#include "PluginRegistration.hpp"
#include "FastMcpLogger.hpp"
#include "RestImplementation.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/OpenApiAggregator.hpp"

// ===========================================================================
// Global state (referenced by PluginRegistration.cpp via extern)
// ===========================================================================

std::atomic_bool     gStopRequested{ false };
std::atomic_bool     gMcpDebug{ false };
std::atomic_uint64_t gMcpServerInstanceCounter{ 0 };
std::mutex           gLogMutex;

namespace
{

using json        = nlohmann::json;
using StringUtils = fastmcp::FastMcpStringUtils;
using McpJson     = mcp::jsonrpc::JsonValue;

constexpr int kDefaultServerPort = CMDSDK_SERVER_PORT;

using ProtocolMode = fastmcp::ProtocolMode;

struct ServerConfig
{
    int                              port          = kDefaultServerPort;
    std::vector<std::filesystem::path> plugin_paths;
    ProtocolMode                     protocol_mode = ProtocolMode::ALL;
};

// ── Logging (also called from PluginRegistration.cpp via extern) ──────────

} // anonymous namespace

namespace
{

std::string currentExceptionMessage()
{
    try { throw; }
    catch (const std::exception& ex) { return ex.what(); }
    catch (...)                       { return "non-std exception"; }
}

bool envFlagEnabled(const char* name, bool default_value = false)
{
    const char* value = std::getenv(name);
    if (!value) return default_value;
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !(v == "0" || v == "false" || v == "off" || v == "no");
}

void handleSignal(int) { gStopRequested.store(true); }

McpJson toMcpJson(const json& value) { return McpJson::parse(value.dump()); }
json fromMcpJson(const McpJson& value) { return json::parse(value.to_string()); }

McpJson makeTextContent(const std::string& text)
{
    McpJson block = McpJson::object();
    block["type"] = "text";
    block["text"] = text;
    return block;
}

// ── Argument parsing ──────────────────────────────────────────────────────

ServerConfig parseArguments(int argc, char** argv)
{
    ServerConfig cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (StringUtils::beginsWith(arg, "--port="))
        {
            cfg.port = std::stoi(arg.substr(7));
        }
        else if (StringUtils::beginsWith(arg, "--plugins="))
        {
            std::stringstream ss(arg.substr(10));
            std::string p;
            while (std::getline(ss, p, ','))
                if (!p.empty()) cfg.plugin_paths.emplace_back(p);
        }
        else if (StringUtils::beginsWith(arg, "--protocol="))
        {
            std::string m = arg.substr(11);
            if      (m == "mcp")  cfg.protocol_mode = ProtocolMode::MCP_ONLY;
            else if (m == "rest") cfg.protocol_mode = ProtocolMode::REST_ONLY;
            else if (m == "all")  cfg.protocol_mode = ProtocolMode::ALL;
            else { std::cerr << "Invalid protocol: " << m << '\n'; exit(1); }
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n'
                      << "Usage: " << argv[0]
                      << " [--port=PORT] [--plugins=P1,P2,...] [--protocol=mcp|rest|all]\n";
            exit(1);
        }
    }
    return cfg;
}

// ── RuntimeResource (kept in main; only MCP resource registration uses it) ─

struct RuntimeResource
{
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
    std::function<std::string()> body_provider;
};

bool addRuntimeResource(std::vector<RuntimeResource>& resources,
                        std::set<std::string>& seen_resource_uris,
                        RuntimeResource resource,
                        bool mcp_debug)
{
    if (!seen_resource_uris.insert(resource.uri).second)
    {
        if (mcp_debug)
            fastmcp::logDiag("MCP-RESOURCE", "Skipping duplicate resource URI " + resource.uri);
        return false;
    }
    resources.push_back(std::move(resource));
    return true;
}

// ── MCP server factory ─────────────────────────────────────────────────────

std::shared_ptr<mcp::server::Server>
createConfiguredMcpServer(const cmdsdk::CommandRegistry& registry,
                          const std::vector<RuntimeResource>& resources)
{
    const auto server_instance_id = ++gMcpServerInstanceCounter;
    const bool mcp_debug          = gMcpDebug.load();

    if (mcp_debug)
        fastmcp::logDiag("MCP-CONNECT",
            "Creating MCP server instance #" + std::to_string(server_instance_id) +
            " (new initialize attempt/session)");

    try
    {
        const mcp::ErrorReporter sdk_error_reporter =
            [mcp_debug](const mcp::ErrorEvent& event)
        {
            if (!mcp_debug) return;
            fastmcp::logDiag("MCP-SDK-ERROR",
                std::string(event.component()) + ": " + std::string(event.message()));
        };

        mcp::lifecycle::session::ToolsCapability tools_capability;
        tools_capability.listChanged = true;

        mcp::lifecycle::session::ResourcesCapability resources_capability;
        resources_capability.listChanged = true;

        mcp::server::ServerConfiguration server_config;
        server_config.sessionOptions.errorReporter = sdk_error_reporter;
        server_config.capabilities = mcp::lifecycle::session::ServerCapabilities(
            std::nullopt, std::nullopt, std::nullopt,
            resources_capability, tools_capability,
            std::nullopt, std::nullopt);
        server_config.serverInfo    = mcp::lifecycle::session::Implementation(
            "fastmcp_server", "0.3.0");
        server_config.instructions  =
            "Use tools for command execution and resources for plugin/app metadata. "
            "UI-enabled commands can expose app tools via CommandMetadata.is_app_tool "
            "and resource_uri.";

        auto mcp_server = mcp::server::Server::create(std::move(server_config));

        // ── Register tools ─────────────────────────────────────────────────
        fastmcp::ToolRegistrationState registration_state;

        for (const auto& meta : registry.listMetadata())
        {
            const auto cmd_name = meta.cmd_name;

            const auto cmd_handler = [&registry, cmd_name](const json& args) -> json
            {
                auto command = registry.create(cmd_name);
                if (!command)
                    throw std::runtime_error("Tool not found: " + cmd_name);
                std::string error;
                if (!command->validate(args, error))
                    throw std::invalid_argument("Validation failed: " + error);
                if (!command->execute(args, error))
                    throw std::runtime_error("Execution failed: " + error);
                return command->getResult();
            };

            if (meta.is_tool)
                fastmcp::registerCmdAsTool(registry, *mcp_server, meta,
                                           cmd_handler, registration_state);

            if (meta.is_app_tool)
                fastmcp::registerAppToolWithUI(registry, *mcp_server, meta,
                                               registration_state);
        }

        // ── Register resources ─────────────────────────────────────────────
        for (const auto& resource : resources)
        {
            mcp::server::ResourceDefinition definition;
            definition.uri         = resource.uri;
            definition.name        = resource.name;
            definition.description = resource.description;
            definition.mimeType    = resource.mime_type;

            if (mcp_debug)
                fastmcp::logDiag("MCP-REGISTER", "Registering resource " + resource.uri);

            mcp_server->registerResource(
                std::move(definition),
                [resource, mcp_debug](const mcp::server::ResourceReadContext& ctx)
                    -> std::vector<mcp::server::ResourceContent>
                {
                    if (mcp_debug)
                        fastmcp::logRequestContext("MCP-RESOURCE-READ", ctx.requestContext,
                            "uri=" + resource.uri);
                    std::string content = resource.body_provider();
                    auto item = mcp::server::ResourceContent::text(
                        resource.uri, content,
                        resource.mime_type + ";profile=mcp-app");
                    return { item };
                });
        }

        return mcp_server;
    }
    catch (...)
    {
        fastmcp::logDiag("MCP-CONNECT",
            "Failed to construct MCP server instance #" + std::to_string(server_instance_id) +
            ": " + currentExceptionMessage());
        throw;
    }
}

} // anonymous namespace

// ===========================================================================
// main
// ===========================================================================
int main(int argc, char** argv)
{
    ServerConfig config = parseArguments(argc, argv);
    gMcpDebug.store(envFlagEnabled("FASTMCP_MCP_DEBUG", false));
    const bool mcp_debug          = gMcpDebug.load();
    const bool require_session_id = envFlagEnabled("FASTMCP_REQUIRE_SESSION_ID", true);

    if (config.plugin_paths.empty())
        config.plugin_paths = fastmcp::getAllPluginsInLib(argv[0]);

    // ── Load plugins ───────────────────────────────────────────────────────
    cmdsdk::CommandRegistry    registry;
    PluginLoader               loader;
    cmdsdk::OpenApiAggregator  api_aggregator;
    bool                       loaded_at_least_one = false;

    for (const auto& path : config.plugin_paths)
    {
        std::string err;
        if (loader.load(path, registry, err))
        {
            loaded_at_least_one = true;
            std::cout << "Loaded plugin: " << path << '\n';
        }
        else
        {
            std::cerr << "Failed to load: " << path << ": " << err << '\n';
        }
    }

    fastmcp::buildOpenApiAggregator(registry, api_aggregator);

    if (!loaded_at_least_one)
        std::cerr << "Warning: no plugins loaded.\n";

    // ── Build MCP resources (external mcp-apps) ───────────────────────────
    std::vector<RuntimeResource> resources;
    std::set<std::string>        seen_resource_uris;

    for (const auto& ext : fastmcp::fetchExternalAppResources())
    {
        addRuntimeResource(resources, seen_resource_uris, RuntimeResource{
            ext.uri,
            ext.name,
            ext.description,
            ext.mime_type,
            [uri = ext.uri]()
            {
                std::string canon, mime, body, err;
                if (fastmcp::readMcpAppResource(uri, canon, mime, body, err))
                    return body;
                return std::string("Error: " + err);
            }
        }, mcp_debug);
    }

    // ── Auxiliary REST server ─────────────────────────────────────────────
    const int rest_port = config.port + 1;
    httplib::Server rest_server;

    fastmcp::registerRestRoutes(rest_server, registry, api_aggregator,
                                config.port, rest_port,
                                config.protocol_mode == ProtocolMode::MCP_ONLY);

    // ── Banner ─────────────────────────────────────────────────────────────
    std::cout << "\n========================================\n"
              << "FastMCP Server  (cpp-mcp-sdk by itcv-GmbH)\n"
              << "========================================\n"
              << "Protocol mode : "
              << StringUtils::protocolModeToString(config.protocol_mode) << "\n\n"
              << "MCP  (cpp-mcp-sdk)  → http://0.0.0.0:" << config.port << "/mcp\n"
              << "REST (httplib)  → http://0.0.0.0:" << rest_port << "/\n\n"
              << "REST routes:\n"
              << "  GET  /              — Homepage\n"
              << "  GET  /swagger       — Swagger UI\n"
              << "  GET  /openapi.json  — Combined OpenAPI\n"
              << "  GET  /openapi.yaml  — YAML OpenAPI\n"
              << "  GET  /openapi/{p}   — Per-plugin spec\n"
              << "  GET  /mcp-apps/*    — Proxy to :6543\n";
    if (config.protocol_mode != ProtocolMode::MCP_ONLY)
        std::cout << "  POST /api/{cmd}     — REST execution\n";
    std::cout << "========================================\n\n";

    // ── Start servers ──────────────────────────────────────────────────────
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::unique_ptr<mcp::server::StreamableHttpServerRunner> mcp_runner;
    if (config.protocol_mode != ProtocolMode::REST_ONLY)
    {
        const mcp::server::ServerFactory server_factory =
            [&registry, &resources]()
        {
            return createConfiguredMcpServer(registry, resources);
        };

        mcp::server::StreamableHttpServerRunnerOptions mcp_options;
        mcp_options.transportOptions.http.errorReporter =
            [mcp_debug](const mcp::ErrorEvent& event)
        {
            if (!mcp_debug) return;
            fastmcp::logDiag("MCP-HTTP-ERROR",
                std::string(event.component()) + ": " + std::string(event.message()));
        };
        mcp_options.transportOptions.http.endpoint.bindAddress       = "0.0.0.0";
        mcp_options.transportOptions.http.endpoint.bindLocalhostOnly = false;
        mcp_options.transportOptions.http.endpoint.port =
            static_cast<std::uint16_t>(config.port);
        mcp_options.transportOptions.http.endpoint.path              = "/mcp";
        mcp_options.transportOptions.http.requireSessionId           = require_session_id;

        if (mcp_debug)
        {
            if (require_session_id)
                fastmcp::logDiag("MCP-CONNECT",
                    "Streamable HTTP policy requireSessionId=true (strict multi-session mode). "
                    "Clients must NOT send MCP-Session-Id on initialize; "
                    "server mints MCP-Session-Id on successful initialize response.");
            else
                fastmcp::logDiag("MCP-CONNECT",
                    "Streamable HTTP policy requireSessionId=false (compat mode). "
                    "All HTTP clients share one MCP session; this improves compatibility "
                    "with proxies that do not replay MCP-Session-Id consistently.");
        }

        mcp_runner = std::make_unique<mcp::server::StreamableHttpServerRunner>(
            server_factory, mcp_options);
        mcp_runner->start();
        std::cout << "MCP  server listening on http://0.0.0.0:" << config.port << "\n";
    }

    if (config.protocol_mode != ProtocolMode::MCP_ONLY)
    {
        std::thread([&rest_server, rest_port]()
        {
            std::cout << "REST server listening on http://0.0.0.0:" << rest_port << '\n';
            if (!rest_server.listen("0.0.0.0", rest_port))
                std::cerr << "ERROR: failed to bind REST server on port " << rest_port << '\n';
        }).detach();
    }

    while (!gStopRequested.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (mcp_runner) mcp_runner->stop();
    rest_server.stop();

    return 0;
}