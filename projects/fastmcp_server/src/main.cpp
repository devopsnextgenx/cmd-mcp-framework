#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <map>
#include <algorithm>
#include <cctype>
#include <algorithm>
#include <cctype>

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

nlohmann::json buildSubtypeResponseSchemas(const cmdsdk::CommandMetadata& metadata) {
  nlohmann::json schemas = nlohmann::json::object();
  for (const auto& subtype : metadata.sub_cmd_types) {
    if (subtype.response_schema.is_object() && !subtype.response_schema.empty()) {
      schemas[subtype.sub_type_name] = subtype.response_schema;
    }
  }
  return schemas;
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

  nlohmann::json tool = {
      {"name",        metadata.cmd_name},
      {"description", metadata.description},
      {"inputSchema", inputSchema}
  };

  const auto subtype_response_schemas = buildSubtypeResponseSchemas(metadata);
  if (!subtype_response_schemas.empty()) {
    nlohmann::json one_of_schemas = nlohmann::json::array();
    nlohmann::json subtype_enum = nlohmann::json::array();
    for (const auto& [subtype_name, schema] : subtype_response_schemas.items()) {
      subtype_enum.push_back(subtype_name);
      one_of_schemas.push_back(schema);
    }

    tool["outputSchema"] = {
        {"type", "object"},
        {"description", "Subtype-specific response schema. Select branch by subTypeExecuted."},
        {"properties", {
            {"subTypeExecuted", {
                {"type", "string"},
                {"enum", subtype_enum},
                {"description", "Indicates which subType produced the response payload."}
            }}
        }},
        {"oneOf", one_of_schemas}
    };
  }

  return tool;
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
using PluginInfo = std::map<std::string, cmdsdk::SubCmdTypeMetadata>;  // subtype_name -> metadata

std::string toUpperAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string canonicalResourceName(const std::string& plugin_name) {
  const auto upper = toUpperAscii(plugin_name);
  if (upper == "GEO") {
    return "geometry";
  }
  if (upper == "MATH") {
    return "math";
  }
  return toLowerAscii(plugin_name);
}

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

std::string openApiPathForCommand(const cmdsdk::CommandMetadata& metadata) {
  return "/api/" + metadata.cmd_name;
}

bool mergeMissingSubtypeEnumsIntoSpec(nlohmann::json& spec,
                                      const cmdsdk::CommandMetadata& metadata) {
  if (!spec.is_object()) {
    return false;
  }

  const auto path = openApiPathForCommand(metadata);
  if (!spec.contains("paths") || !spec["paths"].is_object() ||
      !spec["paths"].contains(path) || !spec["paths"][path].is_object() ||
      !spec["paths"][path].contains("post") || !spec["paths"][path]["post"].is_object()) {
    return false;
  }

  auto& post = spec["paths"][path]["post"];
  if (!post.contains("requestBody") || !post["requestBody"].is_object() ||
      !post["requestBody"].contains("content") || !post["requestBody"]["content"].is_object() ||
      !post["requestBody"]["content"].contains("application/json") ||
      !post["requestBody"]["content"]["application/json"].is_object() ||
      !post["requestBody"]["content"]["application/json"].contains("schema") ||
      !post["requestBody"]["content"]["application/json"]["schema"].is_object()) {
    return false;
  }

  auto& schema = post["requestBody"]["content"]["application/json"]["schema"];
  if (!schema.contains("properties") || !schema["properties"].is_object() ||
      !schema["properties"].contains("subType") || !schema["properties"]["subType"].is_object()) {
    return false;
  }

  auto& sub_type_schema = schema["properties"]["subType"];
  if (!sub_type_schema.contains("enum") || !sub_type_schema["enum"].is_array()) {
    sub_type_schema["enum"] = nlohmann::json::array();
  }

  std::set<std::string> known_subtypes;
  for (const auto& existing_value : sub_type_schema["enum"]) {
    if (existing_value.is_string()) {
      known_subtypes.insert(existing_value.get<std::string>());
    }
  }

  bool changed = false;
  for (const auto& subtype : metadata.sub_cmd_types) {
    if (known_subtypes.insert(subtype.sub_type_name).second) {
      sub_type_schema["enum"].push_back(subtype.sub_type_name);
      changed = true;
    }
  }

  return changed;
}

std::map<std::string, PluginInfo> buildPluginRegistry(const cmdsdk::CommandRegistry& registry) {
  std::map<std::string, PluginInfo> plugins;
  for (const auto& metadata : registry.listMetadata()) {
    const std::string plugin_name = resolvePluginName(metadata);
    for (const auto& subtype : metadata.sub_cmd_types) {
      plugins[plugin_name][subtype.sub_type_name] = subtype;
    }
  }
  return plugins;
}

std::string buildPluginsMarkdown(const std::map<std::string, PluginInfo>& plugins) {
  std::string doc = "# Available Plugins and SubCommand Types\n\n";
  for (const auto& [plugin_name, subtypes] : plugins) {
    doc += "## Plugin: " + plugin_name + "\n\n";
    doc += "### Available SubCommand Types:\n\n";
    for (const auto& [subtype_name, subtype_metadata] : subtypes) {
      doc += "- **" + subtype_name + "**: " + subtype_metadata.description + "\n";
      if (subtype_metadata.response_schema.is_object() &&
          !subtype_metadata.response_schema.empty()) {
        doc += "  - Expected response schema:\n";
        doc += "```json\n" + subtype_metadata.response_schema.dump(2) + "\n```\n";
      }
    }
    doc += "\n";
  }
  return doc;
}

const PluginInfo* findPluginInfo(const std::map<std::string, PluginInfo>& plugins,
                                 const std::string& requested_name,
                                 std::string& resolved_name) {
  const auto exact = plugins.find(requested_name);
  if (exact != plugins.end()) {
    resolved_name = exact->first;
    return &exact->second;
  }

  const auto requested_upper = toUpperAscii(requested_name);
  for (const auto& [plugin_name, plugin_info] : plugins) {
    if (toUpperAscii(plugin_name) == requested_upper) {
      resolved_name = plugin_name;
      return &plugin_info;
    }
  }

  const auto requested_lower = toLowerAscii(requested_name);
  for (const auto& [plugin_name, plugin_info] : plugins) {
    const auto plugin_upper = toUpperAscii(plugin_name);
    if ((requested_lower == "geo" || requested_lower == "geometry") &&
        plugin_upper == "GEO") {
      resolved_name = plugin_name;
      return &plugin_info;
    }
    if (requested_lower == "math" && plugin_upper == "MATH") {
      resolved_name = plugin_name;
      return &plugin_info;
    }
    if (canonicalResourceName(plugin_name) == requested_lower) {
      resolved_name = plugin_name;
      return &plugin_info;
    }
  }

  return nullptr;
}

std::string buildPluginDetailsMarkdown(const std::string& plugin_name,
                                        const PluginInfo& plugin_info) {
  std::string doc = "# Plugin: " + plugin_name + "\n\n";
  doc += "## Available SubCommand Types\n\n";
  for (const auto& [subtype_name, subtype_metadata] : plugin_info) {
    doc += "- **" + subtype_name + "**: " + subtype_metadata.description + "\n";
    if (subtype_metadata.response_schema.is_object() &&
        !subtype_metadata.response_schema.empty()) {
      doc += "  - Expected response schema:\n";
      doc += "```json\n" + subtype_metadata.response_schema.dump(2) + "\n```\n";
    }
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
    std::cout << "Received tools/call request: " << request.dump() << std::endl;
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

    const auto raw_result = command->getResult();
    nlohmann::json structured_result;
    if (raw_result.is_object()) {
      structured_result = raw_result;
    } else {
      structured_result = nlohmann::json{{"result", raw_result}};
    }

    // Include subTypeExecuted if subType was in the request (MCP protocol standard)
    if (args.contains("subType") && args["subType"].is_string()) {
      const auto subtype_executed = args["subType"].get<std::string>();
      structured_result["subTypeExecuted"] = subtype_executed;
    }

    std::cout << "Command execution successful. Raw result: " << raw_result.dump() << std::endl;
    
    return makeJsonRpcResult(id, {
      {"content", {{{"type", "text"}, {"text", raw_result.dump()}}}},
      {"structuredContent", structured_result}
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
      const auto resource_name = canonicalResourceName(plugin_name);
      resources.push_back({
          {"uri",         "plugin://" + resource_name},
          {"name",        "Plugin: " + resource_name},
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

    std::string requested_resource = uri;
    if (uri.starts_with("plugin://")) {
      requested_resource = uri.substr(9);
    }

    if (!requested_resource.empty()) {
      std::string resolved_name;
      const auto* plugin_info = findPluginInfo(plugins, requested_resource, resolved_name);
      if (plugin_info != nullptr) {
        const auto canonical_uri = "plugin://" + canonicalResourceName(resolved_name);
        return makeJsonRpcResult(id, {
            {"contents", {{
                {"uri",      canonical_uri},
                {"mimeType", "text/markdown"},
                {"text",     buildPluginDetailsMarkdown(resolved_name, *plugin_info)}
            }}}
        });
      }
      return makeJsonRpcError(id, -32001, "Plugin not found: " + requested_resource);
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

  std::set<std::string> seen_filenames;
  std::vector<std::filesystem::path> plugins;

  for (const auto& dir : candidate_directories) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      continue;
    }

    std::vector<std::filesystem::path> directory_entries;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (!entry.is_regular_file()) continue;

      const auto& path = entry.path();
      if (!isPluginLibrary(path)) continue;

      const auto filename = path.filename().string();
      if (kNonPluginLibs.count(filename)) {
        std::cout << "Skipping SDK library (not a plugin): " << filename << '\n';
        continue;
      }

      directory_entries.push_back(std::filesystem::absolute(path));
    }

    std::sort(directory_entries.begin(), directory_entries.end(),
              [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
                const auto lhs_name = lhs.filename().string();
                const auto rhs_name = rhs.filename().string();
                if (lhs_name != rhs_name) {
                  return lhs_name < rhs_name;
                }
                return lhs.string() < rhs.string();
              });

    for (const auto& path : directory_entries) {
      std::string filename = path.filename().string();
#if defined(_WIN32)
      filename = toUpperAscii(filename);
#endif
      if (!seen_filenames.insert(filename).second) {
        std::cout << "Skipping duplicate plugin library: " << path.filename().string() << '\n';
        continue;
      }

      plugins.push_back(path);
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

std::string defaultHtmlPage(int, ProtocolMode,
               const std::map<std::string, PluginInfo>&) {
  return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=yes">
  <title>FastMCP · Command Server Control Hub</title>
  <!-- Bootstrap 5 Dark Theme with custom blue accent & rounded soft corners -->
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0-alpha1/dist/css/bootstrap.min.css" rel="stylesheet">
  <!-- Font Awesome 6 (free icons) -->
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css">
  <!-- Google Fonts: Inter for clean modern typography -->
  <link href="https://fonts.googleapis.com/css2?family=Inter:opsz,wght@14..32,300;14..32,400;14..32,500;14..32,600;14..32,700&display=swap" rel="stylesheet">
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }

    body {
      background: radial-gradient(circle at 10% 20%, #0a0f1a, #03060c);
      font-family: 'Inter', system-ui, -apple-system, 'Segoe UI', sans-serif;
      color: #eef2ff;
      padding: 2rem 1.5rem;
      min-height: 100vh;
    }

    /* custom blue accent + rounded corners everywhere */
    .card, .navbar, .list-group-item, .btn, .badge, pre, .endpoint-card, .swagger-card, .status-badge {
      border-radius: 14px !important;
    }

    .card {
      background: rgba(18, 24, 34, 0.85);
      backdrop-filter: blur(2px);
      border: 1px solid rgba(0, 150, 255, 0.2);
      box-shadow: 0 12px 28px -8px rgba(0, 0, 0, 0.5), 0 0 0 1px rgba(0, 150, 255, 0.1);
      transition: all 0.2s ease;
    }

    .card:hover {
      border-color: rgba(0, 150, 255, 0.5);
      box-shadow: 0 20px 32px -12px rgba(0, 120, 255, 0.2);
    }

    .btn-primary {
      background: #0a6cff;
      border: none;
      border-radius: 12px;
      font-weight: 500;
      padding: 0.5rem 1.2rem;
      transition: 0.2s;
      box-shadow: 0 1px 2px rgba(0,0,0,0.2);
    }

    .btn-primary:hover {
      background: #2a7eff;
      transform: translateY(-1px);
      box-shadow: 0 6px 14px rgba(0, 110, 255, 0.3);
    }

    .btn-outline-accent {
      border: 1px solid #2b7aff;
      color: #aad0ff;
      border-radius: 12px;
      background: transparent;
    }
    .btn-outline-accent:hover {
      background: #0a6cff20;
      color: white;
      border-color: #4c9aff;
    }

    .bg-blue-glow {
      background: linear-gradient(135deg, #0950c0 0%, #0a6cff 100%);
    }

    .navbar-brand i {
      font-size: 1.8rem;
      filter: drop-shadow(0 2px 4px rgba(0,0,0,0.3));
    }

    .badge-mcp {
      background: #0e2a3e;
      color: #6bc8ff;
      border-left: 3px solid #0a6cff;
      border-radius: 20px;
      padding: 6px 12px;
      font-weight: 500;
    }

    .endpoint-path {
      font-family: 'SF Mono', 'Fira Code', monospace;
      font-size: 0.9rem;
      background: #0f131c;
      padding: 6px 12px;
      border-radius: 40px;
      border: 1px solid #1e2a3a;
      letter-spacing: -0.2px;
    }

    code {
      background: #0b0f16;
      color: #bbd9ff;
      padding: 0.2rem 0.5rem;
      border-radius: 10px;
      font-size: 0.85rem;
    }

    pre {
      background: #0b0f16;
      padding: 1rem;
      border: 1px solid #1a2533;
      color: #c7e2ff;
      font-size: 0.8rem;
    }

    .swagger-badge {
      background: #1a2a33;
      color: #36c7ff;
    }

    .footer-note {
      font-size: 0.75rem;
      border-top: 1px solid #16212b;
    }

    .list-group-item {
      background-color: #11171f;
      border: 1px solid #1e2c38;
      margin-bottom: 6px;
      color: #deecff;
    }

    .list-group-item i {
      color: #3c9eff;
      width: 28px;
    }

    hr {
      background: #1e3347;
      opacity: 0.5;
    }

    .glowing-icon {
      text-shadow: 0 0 4px #0a6cff;
    }

    @media (max-width: 768px) {
      body {
        padding: 1rem;
      }
    }
  </style>
</head>
<body>

<div class="container-lg px-0 px-md-2">
  <!-- header / navbar -->
  <nav class="navbar navbar-dark mb-4 p-3 shadow-sm" style="background: rgba(8, 12, 20, 0.7); backdrop-filter: blur(10px); border-radius: 20px; border: 1px solid #1f3243;">
    <div class="container-fluid">
      <a class="navbar-brand d-flex align-items-center gap-3" href="#">
        <i class="fas fa-microchip fa-fw glowing-icon" style="color: #3c9eff;"></i>
        <span class="fw-semibold fs-4 tracking-wide">FastMCP <span style="color: #7bb8ff;">Command Server</span></span>
        <span class="badge bg-dark text-info border border-info-subtle ms-2 fs-6"><i class="fas fa-plug me-1"></i> MCP + REST</span>
      </a>
      <div class="d-flex gap-2">
        <div class="d-none d-md-flex align-items-center gap-2">
          <i class="fas fa-circle text-success" style="font-size: 0.7rem;"></i>
          <span class="text-light-emphasis small">Active · port 5432</span>
        </div>
        <button class="btn btn-outline-accent btn-sm px-3" id="copyServerUrlBtn" title="Copy server URL">
          <i class="fas fa-copy me-1"></i> <span class="d-none d-sm-inline">Copy Base URL</span>
        </button>
      </div>
    </div>
  </nav>

  <!-- main info row: MCP + Server endpoints -->
  <div class="row g-4 mb-5">
    <div class="col-lg-5">
      <div class="card h-100 p-3">
        <div class="card-body">
          <div class="d-flex align-items-center gap-3 mb-3">
            <i class="fas fa-network-wired fa-2x" style="color: #2e8bff;"></i>
            <h3 class="card-title mb-0 fw-semibold">MCP Streamable HTTP</h3>
            <span class="badge bg-primary ms-auto">JSON-RPC 2.0</span>
          </div>
          <p class="text-secondary-emphasis mb-2">Endpoint for tool discovery, initialization, and command invocation.</p>
          <div class="bg-dark bg-opacity-50 p-3 rounded-3 mb-3">
            <div class="d-flex align-items-center flex-wrap gap-2">
              <i class="fas fa-link text-info"></i>
              <code class="fs-6 flex-grow-1">http://localhost:5432/mcp</code>
              <button class="btn btn-sm btn-outline-light btn-outline-accent" id="copyMCPUrlBtn"><i class="far fa-copy"></i></button>
            </div>
          </div>
          <div class="mt-3">
            <div class="badge-mcp d-inline-flex mb-2"><i class="fas fa-code-branch me-2"></i> MCP workflow</div>
            <ol class="list-unstyled small mt-2" style="opacity:0.9">
              <li class="mb-1"><i class="fas fa-hand-peace me-2"></i> <code>initialize</code> - handshake protocol</li>
              <li class="mb-1"><i class="fas fa-list me-2"></i> <code>tools/list</code> - discover all exposed commands</li>
              <li class="mb-1"><i class="fas fa-terminal me-2"></i> <code>tools/call</code> - execute native C/C++ tools</li>
            </ol>
            <button class="btn btn-primary w-100 mt-2" id="testDiscoverBtn">
              <i class="fas fa-flask me-2"></i> Simulate tools/list (mock preview)
            </button>
          </div>
        </div>
      </div>
    </div>

    <div class="col-lg-7">
      <div class="card h-100 p-3">
        <div class="card-body">
          <div class="d-flex align-items-center gap-2 mb-3 flex-wrap">
            <i class="fas fa-swagger fa-2x" style="color: #5bc0ff;"></i>
            <h3 class="card-title fw-semibold mb-0">REST API (Swagger UI)</h3>
            <span class="badge swagger-badge ms-2"><i class="fas fa-file-alt"></i> OpenAPI 3.0</span>
            <span class="badge bg-secondary ms-auto">Coming with C++ backend</span>
          </div>
          <p>Full RESTful endpoints exposing same command set with Swagger documentation. Interactive API console & schema.</p>
          <div class="row mt-2">
            <div class="col-md-6 mb-2">
              <div class="bg-dark bg-opacity-40 p-2 rounded-3 d-flex align-items-center gap-2">
                <i class="fas fa-globe text-info"></i>
                <code>GET /swagger/v1/swagger.json</code>
              </div>
            </div>
            <div class="col-md-6 mb-2">
              <div class="bg-dark bg-opacity-40 p-2 rounded-3 d-flex align-items-center gap-2">
                <i class="fas fa-chalkboard-user text-info"></i>
                <code>GET /swagger</code> - UI
              </div>
            </div>
            <div class="col-md-6 mb-2">
              <div class="bg-dark bg-opacity-40 p-2 rounded-3 d-flex align-items-center gap-2">
                <i class="fas fa-plug text-info"></i>
                <code>POST /api/v1/execute</code>
              </div>
            </div>
            <div class="col-md-6 mb-2">
              <div class="bg-dark bg-opacity-40 p-2 rounded-3 d-flex align-items-center gap-2">
                <i class="fas fa-cogs text-info"></i>
                <code>GET /api/v1/tools</code>
              </div>
            </div>
          </div>
          <hr class="my-3">
          <div class="alert alert-dark border-info bg-black bg-opacity-25" role="alert">
            <i class="fas fa-info-circle me-2 text-info"></i>
            <strong>Swagger UI endpoint</strong> will be hosted at <code class="text-white">http://localhost:5432/swagger</code> once enabled.
            Interactive OpenAPI docs with blue theme & dark mode compatible.
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- Endpoint cards: Detailed section for both MCP and future REST endpoints -->
  <div class="row g-4 mb-5">
    <div class="col-12">
      <div class="card p-3">
        <div class="card-header bg-transparent border-bottom border-secondary d-flex align-items-center justify-content-between flex-wrap">
          <span><i class="fas fa-plug me-2 text-primary"></i><strong>Available Service Endpoints</strong> <span class="badge bg-primary rounded-pill ms-2">active</span></span>
          <span class="text-muted small"><i class="fas fa-microphone-alt"></i> MCP + REST dual protocol</span>
        </div>
        <div class="card-body">
          <div class="table-responsive">
            <table class="table table-dark table-hover align-middle" style="border-radius: 14px; overflow: hidden;">
              <thead class="bg-black bg-opacity-50">
                <tr>
                  <th>Method</th>
                  <th>Endpoint</th>
                  <th>Protocol</th>
                  <th>Description</th>
                  <th class="text-center">Try</th>
                </tr>
              </thead>
              <tbody>
                <tr>
                  <td><span class="badge bg-info text-dark">POST</span></td>
                  <td><code>/mcp</code></td>
                  <td><i class="fab fa-connectdevelop me-1"></i> MCP JSON-RPC</td>
                  <td>Main entry for MCP initialize, tools/list, tool call</td>
                  <td class="text-center"><button class="btn btn-outline-accent btn-sm mockMcpCall" data-endpoint="/mcp"><i class="fas fa-vial"></i></button></td>
                </tr>
                <tr class="opacity-75">
                  <td><span class="badge bg-secondary">GET</span></td>
                  <td><code>/swagger</code></td>
                  <td><i class="fas fa-scroll"></i> REST / OpenAPI</td>
                  <td>Interactive Swagger UI (planned / active)</td>
                  <td class="text-center"><span class="badge bg-dark text-info">soon</span></td>
                </tr>
                <tr class="opacity-75">
                  <td><span class="badge bg-secondary">GET</span></td>
                  <td><code>/swagger/v1/swagger.json</code></td>
                  <td><i class="fas fa-code"></i> OpenAPI spec</td>
                  <td>Machine-readable API schema</td>
                  <td class="text-center"><i class="fas fa-hourglass-half text-muted"></i></td>
                </tr>
                <tr>
                  <td><span class="badge bg-warning text-dark">GET</span></td>
                  <td><code>/health</code></td>
                  <td><i class="fas fa-heartbeat"></i> REST</td>
                  <td>Server health & version info</td>
                  <td class="text-center"><button class="btn btn-outline-accent btn-sm" id="healthCheckBtn"><i class="fas fa-stethoscope"></i></button></td>
                </tr>
                <tr>
                  <td><span class="badge bg-success">POST</span></td>
                  <td><code>/api/v1/commands</code></td>
                  <td><i class="fas fa-terminal"></i> REST (future)</td>
                  <td>Native command execution over REST</td>
                  <td class="text-center"><i class="fas fa-cogs text-info"></i></td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- dynamic response & mock explorer (demo integration) -->
  <div class="row g-4">
    <div class="col-md-6">
      <div class="card h-100 p-3">
        <div class="d-flex align-items-center gap-2 mb-2">
          <i class="fas fa-terminal text-primary"></i>
          <h5 class="mb-0">MCP Discovery Simulator</h5>
          <span class="ms-auto badge bg-dark">demo playground</span>
        </div>
        <p class="small text-secondary">Simulate an MCP handshake + tools/list. This mimics the actual JSON-RPC contract your C++ backend will serve.</p>
        <div class="btn-group w-100 mb-3" role="group">
          <button class="btn btn-outline-accent" id="simInitializeBtn"><i class="fas fa-handshake"></i> initialize</button>
          <button class="btn btn-outline-accent" id="simToolsListBtn"><i class="fas fa-list-ul"></i> tools/list</button>
        </div>
        <div class="bg-black bg-gradient rounded-3 p-3" style="min-height: 220px;">
          <div class="d-flex align-items-center gap-2 text-info mb-2">
            <i class="fas fa-reply-all"></i> <span class="small fw-semibold">Mock response preview:</span>
          </div>
          <pre id="mockResponseArea" class="mb-0" style="font-size: 0.75rem; background: #05090f;"><span class="text-secondary">// Click initialize or tools/list to simulate</span></pre>
        </div>
      </div>
    </div>
    <div class="col-md-6">
      <div class="card h-100 p-3">
        <div class="d-flex align-items-center gap-2 mb-2">
          <i class="fas fa-paintbrush-fine text-primary"></i>
          <h5 class="mb-0">Swagger UI Preview (dark + blue accent)</h5>
          <span class="ms-auto badge bg-primary rounded-pill">design mock</span>
        </div>
        <p class="small text-secondary">Your upcoming REST documentation will adopt this sleek Bootstrap dark theme, with rounded corners and crisp blue accent buttons.</p>
        <div class="mt-2 border border-secondary rounded-3 p-3" style="background: #0b111a;">
          <div class="d-flex flex-wrap gap-2 align-items-center">
            <i class="fab fa-swagger fa-2x" style="color: #85c1ff;"></i>
            <div><strong class="text-light">Swagger UI mockup</strong>
            <div class="small text-secondary"><i class="fas fa-microphone"></i> /api/v1/execute <span class="text-info">POST</span> • <span class="text-info">GET</span> /tools</div>
            </div>
            <button class="btn btn-sm btn-primary ms-auto" disabled><i class="fas fa-external-link-alt"></i> Open (soon)</button>
          </div>
          <hr class="my-2">
          <div class="d-flex gap-2 flex-wrap">
            <span class="badge bg-info text-dark px-3 py-2 rounded-pill"><i class="fas fa-code"></i> try it out</span>
            <span class="badge bg-dark border border-info px-3 py-2 rounded-pill"><i class="fas fa-check-circle text-info"></i> OAuth2 disabled</span>
            <span class="badge bg-dark px-3 py-2">basePath: /api/v1</span>
          </div>
          <div class="mt-3 small">📄 <span class="text-info">OpenAPI</span> will list all compiled C++ commands with request/response schemas.</div>
        </div>
        <div class="mt-3 text-center">
          <i class="fas fa-arrow-right text-primary me-1"></i> <span class="text-muted">Swagger UI will be served at <code class="text-info">http://localhost:5432/swagger</code> after enabling REST flag</span>
        </div>
      </div>
    </div>
  </div>

  <!-- toast & footer -->
  <div class="footer-note mt-5 pt-3 d-flex justify-content-between align-items-center flex-wrap">
    <div><i class="fas fa-database me-1 text-info"></i> FastMCP Command Server | Port <strong>5432</strong> | MCP Streamable HTTP + REST (Swagger ready)</div>
    <div><i class="fas fa-palette me-1"></i> dark UI · blue accent · rounded corners · bootstrap 5</div>
  </div>
</div>

<!-- toast container for copy feedback -->
<div class="position-fixed bottom-0 end-0 p-3" style="z-index: 1100">
  <div id="copyToast" class="toast align-items-center text-bg-dark border-info" role="alert" aria-live="assertive" aria-atomic="true" data-bs-autohide="true" data-bs-delay="2000">
    <div class="d-flex">
      <div class="toast-body"><i class="fas fa-check-circle text-info me-2"></i> URL copied to clipboard!</div>
      <button type="button" class="btn-close btn-close-white me-2 m-auto" data-bs-dismiss="toast"></button>
    </div>
  </div>
</div>

<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0-alpha1/dist/js/bootstrap.bundle.min.js"></script>
<script>
  (function() {
    // DOM references
    const mockArea = document.getElementById('mockResponseArea');
    const copyMCPBtn = document.getElementById('copyMCPUrlBtn');
    const copyServerBtn = document.getElementById('copyServerUrlBtn');
    const healthBtn = document.getElementById('healthCheckBtn');
    const simInitBtn = document.getElementById('simInitializeBtn');
    const simToolsBtn = document.getElementById('simToolsListBtn');
    const testDiscoverBtn = document.getElementById('testDiscoverBtn');
    const copyToastEl = document.getElementById('copyToast');
    let toastInstance = null;
    if (copyToastEl) toastInstance = new bootstrap.Toast(copyToastEl, { delay: 1500 });

    function showCopyToaster() {
      if (toastInstance) toastInstance.show();
      else {
        const fallbackToast = new bootstrap.Toast(copyToastEl);
        fallbackToast.show();
      }
    }

    // copy URLS
    const baseUrl = 'http://localhost:5432';
    const mcpUrl = 'http://localhost:5432/mcp';

    function copyToClipboard(text) {
      navigator.clipboard.writeText(text).then(() => {
        showCopyToaster();
      }).catch(() => {
        alert('Manual copy: ' + text);
      });
    }

    if (copyMCPBtn) copyMCPBtn.addEventListener('click', () => copyToClipboard(mcpUrl));
    if (copyServerBtn) copyServerBtn.addEventListener('click', () => copyToClipboard(baseUrl));

    // healthcheck fetch simulation (real fetch to /health if backend exists)
    if (healthBtn) {
      healthBtn.addEventListener('click', async () => {
        try {
          mockArea.innerText = 'Fetching /health from server...\n';
          const response = await fetch('/health');
          if (response.ok) {
            const data = await response.json();
            mockArea.innerText = JSON.stringify(data, null, 2);
          } else {
            mockArea.innerText = `Health endpoint responded with ${response.status}. Ensure backend is running.\n(Mock: C++ server will return { status: "ok", version: "1.0.0" })`;
          }
        } catch (err) {
          mockArea.innerText = `Cannot reach /health. Backend not active? Simulated response:\n{\n  "status": "healthy",\n  "service": "FastMCP Command Server",\n  "port": 5432,\n  "mcpEnabled": true,\n  "swaggerReady": true\n}`;
        }
      });
    }

    // MCP Simulators: mock JSON-RPC initialize + tools/list responses
    function setMockResponse(content) {
      mockArea.innerText = content;
    }

    function mockInitialize() {
      const initResponse = {
        jsonrpc: "2.0",
        id: 1,
        result: {
          protocolVersion: "2024-11-05",
          capabilities: { tools: { listChanged: true } },
          serverInfo: { name: "FastMCP-CPP", version: "0.2.0" }
        }
      };
      setMockResponse(JSON.stringify(initResponse, null, 2) +
        "\n\n// MCP initialize handshake complete. Server ready for tools/list.");
    }

    function mockToolsList() {
      const toolsResponse = {
        jsonrpc: "2.0",
        id: 2,
        result: {
          tools: [
            { name: "list_directory", description: "List contents of a directory (C++ fs)", inputSchema: { type: "object", properties: { path: { type: "string" } } } },
            { name: "execute_command", description: "Run native system command", inputSchema: { type: "object", properties: { cmd: { type: "string" } } } },
            { name: "get_system_info", description: "Retrieve system stats via C++ syscalls" }
          ]
        }
      };
      setMockResponse(JSON.stringify(toolsResponse, null, 2) +
        "\n\n// Discovered 3 tools from C++ backend. Use tools/call to invoke.");
    }

    if (simInitBtn) simInitBtn.addEventListener('click', mockInitialize);
    if (simToolsBtn) simToolsBtn.addEventListener('click', mockToolsList);
    if (testDiscoverBtn) {
      testDiscoverBtn.addEventListener('click', () => {
        mockToolsList();
      });
    }

    // For "Try" buttons that mock MCP call on the /mcp row
    const mockMcpBtns = document.querySelectorAll('.mockMcpCall');
    mockMcpBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        mockArea.innerText = `MCP Request simulation to /mcp\n\nPOST /mcp\nContent-Type: application/json\n\n{\n  "jsonrpc": "2.0",\n  "method": "tools/list",\n  "id": 1\n}\n\n// Server would respond with the list of compiled commands.\n// Your actual C++ handler will return native command metadata.`;
      });
    });

    // initial default friendly message
    setMockResponse('MCP + REST Dashboard ready.\nClick "initialize" to mock handshake, or "tools/list" to view sample command list.\n\nREST API Swagger UI will be available at /swagger (blue themed).');

    // add small hover effect to show overall endpoints design note
    console.log("Dark themed UI with blue accent and small border radius active | Bootstrap 5");
  })();
</script>
</body>
</html>)HTML";
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
      const bool has_custom_openapi = pm_registry.hasCustomOpenApi(plugin_name);
      const auto command_path = openApiPathForCommand(metadata);

      // Check if this plugin has already been processed for OpenAPI
      if (!processed_plugins[plugin_name]) {
        processed_plugins[plugin_name] = true;

        // Try to load custom OpenAPI spec from PluginMetadataRegistry
        if (has_custom_openapi) {
          // Use custom OpenAPI spec from registry
          api_aggregator.addPluginSpec(plugin_name, pm_registry.getCustomOpenApi(plugin_name),
                                       "registered-metadata");
          std::cout << "    [OpenAPI] Loaded custom spec from PluginMetadataRegistry\n";
        }
      }

      if (has_custom_openapi) {
        auto plugin_spec = api_aggregator.getPluginSpec(plugin_name);

        if (!plugin_spec.contains("paths") || !plugin_spec["paths"].is_object() ||
            !plugin_spec["paths"].contains(command_path)) {
          api_aggregator.addAutoGeneratedSpec(metadata, plugin_name);
          std::cout << "    [OpenAPI] Added missing command from provider metadata: "
                    << metadata.cmd_name << '\n';
        } else if (mergeMissingSubtypeEnumsIntoSpec(plugin_spec, metadata)) {
          api_aggregator.addPluginSpec(plugin_name, plugin_spec, "registered-metadata+provider");
          std::cout << "    [OpenAPI] Added missing provider subTypes to custom spec for: "
                    << metadata.cmd_name << '\n';
        }
      } else {
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
    response.set_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response.set_header("Pragma", "no-cache");
    response.set_header("Expires", "0");
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
    response.set_content(std::string(cmdsdk::swagger_resources::swagger_html),
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