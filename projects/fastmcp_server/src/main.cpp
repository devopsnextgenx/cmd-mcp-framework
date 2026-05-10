#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <map>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "PluginLoader.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/OpenApiGenerator.hpp"
#include "cmdsdk/OpenApiAggregator.hpp"
#include "cmdsdk/RestApiHandler.hpp"
#include "cmdsdk/PluginOpenApiLoader.hpp"
#include "cmdsdk/PluginMetadata.hpp"
#include "cmdsdk/SwaggerResources.hpp"

namespace {

constexpr int kDefaultServerPort = CMDSDK_SERVER_PORT;

// ---------------------------------------------------------------------------
// Known SDK libraries that live in lib/ but are NOT plugins.
// Extend this list if more SDK shared libs are added.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
static const std::set<std::string> kNonPluginLibs = {
  "cmd_sdk.dll",
};
#elif defined(__APPLE__)
static const std::set<std::string> kNonPluginLibs = {
  "libcmd_sdk.dylib",
};
#else
static const std::set<std::string> kNonPluginLibs = {
  "libcmd_sdk.so",
};
#endif

// Protocol modes for endpoint support
enum class ProtocolMode {
  MCP_ONLY,   // Only MCP endpoints
  REST_ONLY,  // Only REST endpoints
  ALL         // Both MCP and REST endpoints (default)
};

struct ServerConfig {
  int port = kDefaultServerPort;
  std::vector<std::filesystem::path> plugin_paths;
  ProtocolMode protocol_mode = ProtocolMode::ALL;
};

ServerConfig parseArguments(int argc, char** argv) {
  ServerConfig config;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.starts_with("--port=")) {
      config.port = std::stoi(arg.substr(7));
    } else if (arg.starts_with("--plugins=")) {
      std::string plugins_str = arg.substr(10);
      std::stringstream ss(plugins_str);
      std::string plugin;
      while (std::getline(ss, plugin, ',')) {
        if (!plugin.empty()) {
          config.plugin_paths.emplace_back(plugin);
        }
      }
    } else if (arg.starts_with("--protocol=")) {
      std::string mode_str = arg.substr(11);
      if (mode_str == "mcp") {
        config.protocol_mode = ProtocolMode::MCP_ONLY;
      } else if (mode_str == "rest") {
        config.protocol_mode = ProtocolMode::REST_ONLY;
      } else if (mode_str == "all") {
        config.protocol_mode = ProtocolMode::ALL;
      } else {
        std::cerr << "Invalid protocol mode: " << mode_str << ". Use: mcp, rest, or all\n";
        exit(1);
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      std::cerr << "Usage: " << argv[0] << " [--port=PORT] [--plugins=PLUGIN1,PLUGIN2,...] [--protocol=mcp|rest|all]\n";
      exit(1);
    }
  }
  return config;
}

void addCorsHeaders(httplib::Response& response) {
  response.set_header("Access-Control-Allow-Origin", "*");
  response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  response.set_header("Access-Control-Allow-Headers", "Content-Type, MCP-Session-Id, MCP-Protocol-Version");
}

nlohmann::json parameterToMcpSchema(const cmdsdk::ParameterMetadata& parameter,
                                     const nlohmann::json& subtype_options = nullptr) {
  std::string type = "string";
  if (parameter.parameter_type == "number")       type = "number";
  else if (parameter.parameter_type == "boolean") type = "boolean";
  else if (parameter.parameter_type == "object")  type = "object";
  else if (parameter.parameter_type == "array")   type = "array";

  nlohmann::json schema = {
      {"type",        type},
      {"description", parameter.description}
  };

  if (parameter.parameter_name == "subType" &&
      subtype_options != nullptr && subtype_options.is_array()) {
    schema["enum"] = subtype_options;
  }

  return schema;
}

nlohmann::json commandToMcpTool(const cmdsdk::CommandMetadata& metadata) {
  nlohmann::json subtype_enums = nlohmann::json::array();
  for (const auto& subtype : metadata.sub_cmd_types) {
    subtype_enums.push_back(subtype.sub_type_name);
  }

  nlohmann::json inputSchema = {
      {"type",       "object"},
      {"properties", nlohmann::json::object()},
      {"required",   nlohmann::json::array()}
  };

  for (const auto& param : metadata.parameters) {
    nlohmann::json param_schema =
        (param.parameter_name == "subType" && !subtype_enums.empty())
            ? parameterToMcpSchema(param, subtype_enums)
            : parameterToMcpSchema(param);
    inputSchema["properties"][param.parameter_name] = param_schema;
    if (param.required) {
      inputSchema["required"].push_back(param.parameter_name);
    }
  }

  return {
      {"name",        metadata.cmd_name},
      {"description", metadata.description},
      {"inputSchema", inputSchema}
  };
}

nlohmann::json makeJsonRpcResult(const nlohmann::json& id, const nlohmann::json& result) {
  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

nlohmann::json makeJsonRpcError(const nlohmann::json& id, int code, const std::string& message) {
  return {
      {"jsonrpc", "2.0"},
      {"id",      id},
      {"error",   {{"code", code}, {"message", message}}}
  };
}

// ---------------------------------------------------------------------------
// Plugin registry helpers
//
// A "plugin name" is derived from the plugin_name field set via
// SubCmd::setPluginName() and exposed through CommandMetadata::plugin_name.
//
// Fallback: if plugin_name is empty, extract the first dot-segment of the
// first sub_cmd_type name (e.g. "MATH" from "MATH.ADD").
// ---------------------------------------------------------------------------
using PluginInfo = std::map<std::string, std::string>;  // subtype_name -> description

std::string resolvePluginName(const cmdsdk::CommandMetadata& metadata) {
  // Prefer the explicit plugin_name field populated by setPluginName().
  if (!metadata.plugin_name.empty()) {
    return metadata.plugin_name;
  }
  // Fallback: first dot-segment of the first registered sub-type name.
  if (!metadata.sub_cmd_types.empty()) {
    const auto& first = metadata.sub_cmd_types.front().sub_type_name;
    const auto dot = first.find('.');
    if (dot != std::string::npos) {
      return first.substr(0, dot);
    }
    return first;
  }
  // Last resort: use the command name itself.
  return metadata.cmd_name;
}

std::map<std::string, PluginInfo> buildPluginRegistry(const cmdsdk::CommandRegistry& registry) {
  std::map<std::string, PluginInfo> plugins;
  for (const auto& metadata : registry.listMetadata()) {
    const std::string plugin_name = resolvePluginName(metadata);
    for (const auto& subtype : metadata.sub_cmd_types) {
      plugins[plugin_name][subtype.sub_type_name] = subtype.description;
    }
  }
  return plugins;
}

std::string buildPluginsMarkdown(const std::map<std::string, PluginInfo>& plugins) {
  std::string doc = "# Available Plugins and SubCommand Types\n\n";
  for (const auto& [plugin_name, subtypes] : plugins) {
    doc += "## Plugin: " + plugin_name + "\n\n";
    doc += "### Available SubCommand Types:\n\n";
    for (const auto& [subtype_name, description] : subtypes) {
      doc += "- **" + subtype_name + "**: " + description + "\n";
    }
    doc += "\n";
  }
  return doc;
}

std::string buildPluginDetailsMarkdown(const std::string& plugin_name,
                                        const PluginInfo& plugin_info) {
  std::string doc = "# Plugin: " + plugin_name + "\n\n";
  doc += "## Available SubCommand Types\n\n";
  for (const auto& [subtype_name, description] : plugin_info) {
    doc += "- **" + subtype_name + "**: " + description + "\n";
  }
  return doc;
}

// ---------------------------------------------------------------------------
// MCP request dispatcher
// ---------------------------------------------------------------------------
nlohmann::json handleMcpRequest(const nlohmann::json& request,
                                 cmdsdk::CommandRegistry& registry) {
  const auto id = request.contains("id") ? request.at("id") : nlohmann::json(nullptr);

  if (!request.is_object())
    return makeJsonRpcError(id, -32600, "Invalid Request: body must be an object.");
  if (!request.contains("jsonrpc") || request.at("jsonrpc") != "2.0")
    return makeJsonRpcError(id, -32600, "Invalid Request: jsonrpc must be 2.0.");
  if (!request.contains("method") || !request.at("method").is_string())
    return makeJsonRpcError(id, -32600, "Invalid Request: method must be a string.");

  const auto method = request.at("method").get<std::string>();

  // ── initialize ────────────────────────────────────────────────────────────
  if (method == "initialize") {
    return makeJsonRpcResult(id, {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools",     {{"listChanged", true}}},
            {"resources", {{"listChanged", true}}}
        }},
        {"serverInfo", {{"name", "fastmcp_server"}, {"version", "0.1.0"}}}
    });
  }

  // ── tools/list ────────────────────────────────────────────────────────────
  if (method == "tools/list") {
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& metadata : registry.listMetadata()) {
      tools.push_back(commandToMcpTool(metadata));
    }
    return makeJsonRpcResult(id, {{"tools", tools}});
  }

  // ── tools/call ────────────────────────────────────────────────────────────
  if (method == "tools/call") {
    if (!request.contains("params") || !request.at("params").is_object())
      return makeJsonRpcError(id, -32602, "Invalid params: params must be an object.");

    const auto& params = request.at("params");
    if (!params.contains("name") || !params.at("name").is_string())
      return makeJsonRpcError(id, -32602, "Invalid params: name must be a string.");

    const auto cmd_name = params.at("name").get<std::string>();
    const auto args = params.contains("arguments")
                          ? params.at("arguments")
                          : nlohmann::json::object();
    if (!args.is_object())
      return makeJsonRpcError(id, -32602, "Invalid params: arguments must be a JSON object.");

    auto command = registry.create(cmd_name);
    if (!command)
      return makeJsonRpcError(id, -32601, "Tool not found: " + cmd_name);

    std::string error;
    if (!command->validate(args, error))
      return makeJsonRpcError(id, -32000, "Validation failed: " + error);
    if (!command->execute(args, error))
      return makeJsonRpcError(id, -32001, "Execution failed: " + error);

    return makeJsonRpcResult(id, {
        {"content", {{{"type", "text"}, {"text", command->getResult().dump()}}}}
    });
  }

  // ── resources/list ────────────────────────────────────────────────────────
  if (method == "resources/list") {
    auto plugins = buildPluginRegistry(registry);
    nlohmann::json resources = nlohmann::json::array();

    resources.push_back({
        {"uri",         "plugins://overview"},
        {"name",        "Plugins Overview"},
        {"description", "Overview of all available plugins and their SubCommand types"},
        {"mimeType",    "text/markdown"}
    });

    for (const auto& [plugin_name, _] : plugins) {
      resources.push_back({
          {"uri",         "plugin://" + plugin_name},
          {"name",        "Plugin: " + plugin_name},
          {"description", "Details for " + plugin_name + " plugin including available SubCommand types"},
          {"mimeType",    "text/markdown"}
      });
    }

    return makeJsonRpcResult(id, {{"resources", resources}});
  }

  // ── resources/read ────────────────────────────────────────────────────────
  if (method == "resources/read") {
    if (!request.contains("params") || !request.at("params").is_object())
      return makeJsonRpcError(id, -32602, "Invalid params: params must be an object.");

    const auto& params = request.at("params");
    if (!params.contains("uri") || !params.at("uri").is_string())
      return makeJsonRpcError(id, -32602, "Invalid params: uri must be a string.");

    const std::string uri = params.at("uri").get<std::string>();
    auto plugins = buildPluginRegistry(registry);

    if (uri == "plugins://overview") {
      return makeJsonRpcResult(id, {
          {"contents", {{
              {"uri",      uri},
              {"mimeType", "text/markdown"},
              {"text",     buildPluginsMarkdown(plugins)}
          }}}
      });
    }

    if (uri.starts_with("plugin://")) {
      const std::string plugin_name = uri.substr(9);
      auto it = plugins.find(plugin_name);
      if (it != plugins.end()) {
        return makeJsonRpcResult(id, {
            {"contents", {{
                {"uri",      uri},
                {"mimeType", "text/markdown"},
                {"text",     buildPluginDetailsMarkdown(plugin_name, it->second)}
            }}}
        });
      }
      return makeJsonRpcError(id, -32001, "Plugin not found: " + plugin_name);
    }

    return makeJsonRpcError(id, -32001, "Resource not found: " + uri);
  }

  return makeJsonRpcError(id, -32601, "Method not found.");
}

// ---------------------------------------------------------------------------
// Plugin discovery
//
// Scans <executable>/../lib/ for shared libraries, skipping known SDK libs
// (libcmd_sdk.so) that are not plugins and would cause spurious load errors.
// ---------------------------------------------------------------------------
std::vector<std::filesystem::path> getAllPluginsInLib(const char* argv0) {
  const auto executable_path      = std::filesystem::absolute(argv0);
  const auto executable_directory = executable_path.parent_path();
  const auto parent_directory     = executable_directory.parent_path();

  std::vector<std::filesystem::path> candidate_directories;
  candidate_directories.push_back(executable_directory);
  if (!parent_directory.empty()) {
    candidate_directories.push_back(parent_directory);
    candidate_directories.push_back(parent_directory / "lib");

    const auto build_root_directory = parent_directory.parent_path();
    if (!build_root_directory.empty()) {
      candidate_directories.push_back(build_root_directory / "lib");
      candidate_directories.push_back(build_root_directory / "bin");

      // Handle multi-config generators (Debug/Release/etc.) by checking
      // matching config folders under lib/ and bin/.
      const auto config_directory = executable_directory.filename().string();
      if (config_directory == "Debug" || config_directory == "Release" ||
          config_directory == "RelWithDebInfo" || config_directory == "MinSizeRel") {
        candidate_directories.push_back(build_root_directory / "lib" / config_directory);
        candidate_directories.push_back(build_root_directory / "bin" / config_directory);
      }
    }
  }

  auto isPluginLibrary = [](const std::filesystem::path& path) {
    if (!path.has_filename()) return false;
    const auto filename = path.filename().string();
#if defined(_WIN32)
    return path.extension() == ".dll";
#elif defined(__APPLE__)
    return filename.starts_with("lib") && path.extension() == ".dylib";
#else
    return filename.starts_with("lib") && path.extension() == ".so";
#endif
  };

  std::set<std::filesystem::path> seen;
  std::vector<std::filesystem::path> plugins;

  for (const auto& dir : candidate_directories) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      continue;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;

      const auto& path = entry.path();
      if (!isPluginLibrary(path)) continue;

      const auto filename = path.filename().string();
      if (kNonPluginLibs.count(filename)) {
        std::cout << "Skipping SDK library (not a plugin): " << filename << '\n';
        continue;
      }

      const auto absolute_path = std::filesystem::absolute(path);
      if (!seen.insert(absolute_path).second) {
        continue;
      }

      plugins.push_back(absolute_path);
    }
  }

  return plugins;
}

std::string protocolModeToString(ProtocolMode mode) {
  switch (mode) {
    case ProtocolMode::MCP_ONLY:
      return "MCP Only";
    case ProtocolMode::REST_ONLY:
      return "REST Only";
    case ProtocolMode::ALL:
      return "MCP + REST";
    default:
      return "Unknown";
  }
}

std::string defaultHtmlPage(int port, ProtocolMode protocol_mode,
                             const std::map<std::string, PluginInfo>& plugins) {
  std::string endpoints_html;

  endpoints_html += "  <h2>OpenAPI & Swagger</h2>\n";
  endpoints_html += "  <ul>\n";
  endpoints_html += "    <li><a href='/swagger'><strong>GET /swagger</strong></a> — Interactive Swagger UI (Dark Theme)</li>\n";
  endpoints_html += "    <li><code>GET /openapi.json</code> — Combined OpenAPI specification (JSON)</li>\n";
  endpoints_html += "    <li><code>GET /openapi.yaml</code> — Combined OpenAPI specification (YAML)</li>\n";
  endpoints_html += "  </ul>\n";

  if (protocol_mode == ProtocolMode::MCP_ONLY || protocol_mode == ProtocolMode::ALL) {
    endpoints_html += "  <h2>MCP Protocol</h2>\n";
    endpoints_html += "  <ul>\n";
    endpoints_html += "    <li><code>POST /mcp</code> — MCP JSON-RPC endpoint for tools and resources.</li>\n";
    endpoints_html += "  </ul>\n";
    endpoints_html += "  <p><strong>MCP Usage:</strong> Use this server URL: <code>http://localhost:" + std::to_string(port) + "/mcp</code></p>\n";
  }

  if (protocol_mode == ProtocolMode::REST_ONLY || protocol_mode == ProtocolMode::ALL) {
    endpoints_html += "  <h2>REST API</h2>\n";
    endpoints_html += "  <ul>\n";
    endpoints_html += "    <li><code>POST /api/{command}</code> — Execute command via REST.</li>\n";
    endpoints_html += "  </ul>\n";
    endpoints_html += "  <p><strong>REST Usage Example:</strong></p>\n";
    endpoints_html += "  <pre><code>curl -X POST http://localhost:" + std::to_string(port) + "/api/math.calculate \\\n";
    endpoints_html += "  -H 'Content-Type: application/json' \\\n";
    endpoints_html += "  -d '{\"subType\": \"MATH.ADD\", \"left\": 5, \"right\": 3}'</code></pre>\n";
  }

  endpoints_html += "  <h2>Registered Plugins & Providers</h2>\n";
  if (!plugins.empty()) {
    endpoints_html += "  <table border='1' cellpadding='10' cellspacing='0' style='width: 100%;'>\n";
    endpoints_html += "    <tr><th>Plugin</th><th>SubTypes</th><th>Download Spec</th></tr>\n";
    for (const auto& [plugin_name, subtypes] : plugins) {
      endpoints_html += "    <tr><td><strong>" + plugin_name + "</strong></td><td>";
      bool first = true;
      for (const auto& [subtype_name, _] : subtypes) {
        if (!first) endpoints_html += ", ";
        endpoints_html += subtype_name;
        first = false;
      }
      endpoints_html += "</td><td><a href='/openapi/" + plugin_name + ".json'>JSON</a> | <a href='/openapi/" + plugin_name + ".yaml'>YAML</a></td></tr>\n";
    }
    endpoints_html += "  </table>\n";
  } else {
    endpoints_html += "  <p><em>No plugins loaded.</em></p>\n";
  }

  return R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <title>FastMCP Command Server</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { 
      font-family: 'Segoe UI', sans-serif; 
      background: #1e1e1e; 
      color: #e0e0e0; 
      margin: 0; 
      padding: 0; 
    }
    .container {
      max-width: 1000px;
      margin: 0 auto;
      padding: 2rem;
    }
    h1 { 
      color: #4da6ff; 
      margin-bottom: 0.5rem; 
      font-size: 2em;
    }
    h2 { 
      color: #9cdcfe; 
      margin-top: 2rem; 
      margin-bottom: 1rem; 
      font-size: 1.5em;
      border-bottom: 1px solid #404040;
      padding-bottom: 0.5rem;
    }
    p { 
      line-height: 1.6; 
      margin-bottom: 1rem;
      color: #d0d0d0;
    }
    code { 
      background: #2d2d2d; 
      color: #9cdcfe; 
      padding: 0.2rem 0.4rem; 
      border-radius: 3px; 
      font-family: 'Courier New', monospace;
    }
    pre {
      background: #2d2d2d;
      border-left: 3px solid #4da6ff;
      padding: 1rem;
      overflow-x: auto;
      margin: 1rem 0;
      border-radius: 3px;
    }
    pre code {
      background: transparent;
      color: #9cdcfe;
      padding: 0;
    }
    ul { 
      margin-left: 2rem; 
      margin-bottom: 1rem;
    }
    li { 
      margin-bottom: 0.5rem; 
    }
    a { 
      color: #4da6ff; 
      text-decoration: none; 
    }
    a:hover { 
      text-decoration: underline; 
    }
    table {
      border-collapse: collapse;
      width: 100%;
      margin: 1rem 0;
      background: #2d2d2d;
    }
    table th, table td {
      border: 1px solid #404040;
      padding: 0.75rem;
      text-align: left;
    }
    table th {
      background: #1a1a1a;
      color: #4da6ff;
    }
    .badge {
      display: inline-block;
      background: #4da6ff;
      color: #fff;
      padding: 0.25rem 0.75rem;
      border-radius: 20px;
      font-size: 0.85em;
      margin-top: 1rem;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚡ FastMCP Command Server</h1>
    <p>Server is running on <code>localhost:)" + std::to_string(port) + R"(</code></p>
    <p><span class="badge">Protocol: )" + protocolModeToString(protocol_mode) + R"(</span></p>
)" + endpoints_html + R"(
    <hr style="margin: 2rem 0; border: none; border-top: 1px solid #404040;">
    <p style="text-align: center; color: #707070; font-size: 0.9em;">
      FastMCP Framework • OpenAPI + REST + MCP • <a href='/swagger'>View API Docs</a>
    </p>
  </div>
</body>
</html>)";
}

}  // namespace

int main(int argc, char** argv) {
  ServerConfig config = parseArguments(argc, argv);

  if (config.plugin_paths.empty()) {
    config.plugin_paths = getAllPluginsInLib(argv[0]);
  }

  cmdsdk::CommandRegistry registry;
  PluginLoader loader;
  cmdsdk::OpenApiAggregator api_aggregator;

  bool loaded_at_least_one_plugin = false;
  for (const auto& plugin_path : config.plugin_paths) {
    std::string error;
    if (loader.load(plugin_path, registry, error)) {
      loaded_at_least_one_plugin = true;
      std::cout << "Loaded plugin: " << plugin_path << '\n';
    } else {
      std::cerr << "Failed to load plugin " << plugin_path << ": " << error << '\n';
    }
  }

  // Build OpenAPI aggregator from registered commands
  std::map<std::string, bool> processed_plugins;
  const auto& pm_registry = cmdsdk::PluginMetadataRegistry::instance();

  if (!registry.listMetadata().empty()) {
    std::cout << "\nRegistered commands:\n";
    for (const auto& metadata : registry.listMetadata()) {
      const auto plugin_name = resolvePluginName(metadata);

      // Check if this plugin has already been processed for OpenAPI
      if (!processed_plugins[plugin_name]) {
        processed_plugins[plugin_name] = true;

        // Try to load custom OpenAPI spec from PluginMetadataRegistry
        if (pm_registry.hasCustomOpenApi(plugin_name)) {
          // Use custom OpenAPI spec from registry
          api_aggregator.addPluginSpec(plugin_name, pm_registry.getCustomOpenApi(plugin_name),
                                       "registered-metadata");
          std::cout << "    [OpenAPI] Loaded custom spec from PluginMetadataRegistry\n";
        }
      }

      // Auto-generate OpenAPI spec for this command (if not already done via custom spec)
      if (!pm_registry.hasCustomOpenApi(plugin_name)) {
        api_aggregator.addAutoGeneratedSpec(metadata, plugin_name);
      }

      // Print command info
      std::cout << "  - [" << plugin_name << "] " << metadata.cmd_name << ": "
                << metadata.description << '\n';
      for (const auto& subtype : metadata.sub_cmd_types) {
        std::cout << "      - " << subtype.sub_type_name << ": " << subtype.description
                  << '\n';
      }
    }
  }

  if (!loaded_at_least_one_plugin) {
    std::cerr << "No plugins were loaded.\n";
  }

  // Build plugin registry for homepage
  auto plugins = buildPluginRegistry(registry);

  // Create HTTP server
  httplib::Server server;

  // ─────────────────────────────────────────────────────────────────────────
  // GET / — Enhanced homepage with dark theme
  // ─────────────────────────────────────────────────────────────────────────
  server.Get("/", [&](const httplib::Request&, httplib::Response& response) {
    addCorsHeaders(response);
    response.set_content(defaultHtmlPage(config.port, config.protocol_mode, plugins),
                         "text/html");
  });

  // ─────────────────────────────────────────────────────────────────────────
  // OpenAPI Endpoints
  // ─────────────────────────────────────────────────────────────────────────

  // GET /openapi.json — Combined OpenAPI spec
  server.Get("/openapi.json", [&](const httplib::Request&, httplib::Response& response) {
    addCorsHeaders(response);
    const auto combined_spec = api_aggregator.buildCombinedSpec("FastMCP API", "0.1.0");
    response.set_content(combined_spec.dump(2), "application/json");
  });

  // GET /openapi.yaml — Combined OpenAPI spec as YAML (basic conversion)
  server.Get("/openapi.yaml", [&](const httplib::Request&, httplib::Response& response) {
    addCorsHeaders(response);
    const auto combined_spec = api_aggregator.buildCombinedSpec("FastMCP API", "0.1.0");
    // For simplicity, serve as JSON with YAML media type
    // A full implementation would convert to proper YAML
    response.set_content(combined_spec.dump(2), "application/yaml");
  });

  // GET /openapi/{plugin}.json — Individual plugin spec
  server.Get("/openapi/:plugin", [&](const httplib::Request& request,
                                       httplib::Response& response) {
    addCorsHeaders(response);
    const auto plugin_name_with_ext = request.path_params.at("plugin");
    // Strip extension (.json or .yaml)
    std::string plugin_name = plugin_name_with_ext;
    if (plugin_name.ends_with(".json")) {
      plugin_name = plugin_name.substr(0, plugin_name.length() - 5);
    } else if (plugin_name.ends_with(".yaml") || plugin_name.ends_with(".yml")) {
      size_t pos = plugin_name.rfind('.');
      if (pos != std::string::npos) {
        plugin_name = plugin_name.substr(0, pos);
      }
    }

    const auto spec = api_aggregator.getPluginSpec(plugin_name);
    if (spec.is_object() && !spec.empty()) {
      if (plugin_name_with_ext.ends_with(".json")) {
        response.set_content(spec.dump(2), "application/json");
      } else {
        response.set_content(spec.dump(2), "application/yaml");
      }
    } else {
      response.status = 404;
      response.set_content(
          nlohmann::json({{"error", "Plugin spec not found: " + plugin_name}}).dump(),
          "application/json");
    }
  });

  // GET /swagger — Interactive Swagger UI (embedded)
  server.Get("/swagger", [&](const httplib::Request&, httplib::Response& response) {
    addCorsHeaders(response);
    response.set_content(std::string(cmdsdk::swagger_resources::swagger_minimal_html),
                         "text/html");
  });

  // ─────────────────────────────────────────────────────────────────────────
  // REST API Endpoints
  // ─────────────────────────────────────────────────────────────────────────

  // POST /api/{command} — Execute command via REST
  server.Post("/api/:command", [&](const httplib::Request& request,
                                     httplib::Response& response) {
    addCorsHeaders(response);

    const auto command_name = request.path_params.at("command");
    nlohmann::json request_body;

    try {
      request_body = nlohmann::json::parse(request.body);
    } catch (const std::exception& e) {
      response.status = 400;
      response.set_content(
          cmdsdk::RestApiHandler::buildResponse(false, "Invalid JSON: " + std::string(e.what()))
              .dump(),
          "application/json");
      return;
    }

    if (!request_body.is_object()) {
      response.status = 400;
      response.set_content(
          cmdsdk::RestApiHandler::buildResponse(false, "Request body must be a JSON object")
              .dump(),
          "application/json");
      return;
    }

    std::string error;
    const auto result = cmdsdk::RestApiHandler::executeCommand(command_name, request_body,
                                                                registry, error);

    if (!result["success"].get<bool>()) {
      response.status = 400;
    }
    response.set_content(result.dump(), "application/json");
  });

  // ─────────────────────────────────────────────────────────────────────────
  // MCP Endpoints
  // ─────────────────────────────────────────────────────────────────────────

  server.Options("/mcp", [](const httplib::Request&, httplib::Response& response) {
    addCorsHeaders(response);
    response.status = 200;
  });

  server.Post("/mcp", [&](const httplib::Request& request, httplib::Response& response) {
    addCorsHeaders(response);
    nlohmann::json rpc_request;
    try {
      rpc_request = nlohmann::json::parse(request.body);
    } catch (const std::exception& parse_error) {
      const auto error = makeJsonRpcError(nullptr, -32700,
                                          "Parse error: " + std::string(parse_error.what()));
      response.status = 400;
      response.set_content(error.dump(), "application/json");
      return;
    }

    const auto rpc_response = handleMcpRequest(rpc_request, registry);
    response.set_content(rpc_response.dump(2), "application/json");
  });

  // ─────────────────────────────────────────────────────────────────────────
  // Start server
  // ─────────────────────────────────────────────────────────────────────────

  std::cout << "\n========================================\n";
  std::cout << "FastMCP Server Configuration\n";
  std::cout << "========================================\n";
  std::cout << "Port: " << config.port << '\n';
  std::cout << "Protocol Mode: " << protocolModeToString(config.protocol_mode) << '\n';
  std::cout << "\nEndpoints:\n";
  std::cout << "  GET  /                  — Homepage with plugin list\n";
  std::cout << "  GET  /swagger           — Interactive Swagger UI\n";
  std::cout << "  GET  /openapi.json      — Combined OpenAPI spec\n";
  std::cout << "  GET  /openapi.yaml      — Combined OpenAPI spec (YAML)\n";
  std::cout << "  GET  /openapi/{plugin}  — Individual plugin specs\n";

  if (config.protocol_mode == ProtocolMode::REST_ONLY || config.protocol_mode == ProtocolMode::ALL) {
    std::cout << "  POST /api/{command}     — REST API endpoint\n";
  }
  if (config.protocol_mode == ProtocolMode::MCP_ONLY || config.protocol_mode == ProtocolMode::ALL) {
    std::cout << "  POST /mcp               — MCP JSON-RPC endpoint\n";
  }
  std::cout << "========================================\n\n";

  std::cout << "Starting server on http://0.0.0.0:" << config.port << '\n';

  if (!server.listen("0.0.0.0", config.port)) {
    std::cerr << "Failed to bind server on port " << config.port << ".\n";
    return 1;
  }

  return 0;
}