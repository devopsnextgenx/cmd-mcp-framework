#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "PluginLoader.hpp"
#include "cmdsdk/CommandRegistry.hpp"

namespace {

constexpr int kDefaultServerPort = CMDSDK_SERVER_PORT;

struct ServerConfig {
  int port = kDefaultServerPort;
  std::vector<std::filesystem::path> plugin_paths;
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
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      std::cerr << "Usage: " << argv[0] << " [--port=PORT] [--plugins=PLUGIN1,PLUGIN2,...]\n";
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

nlohmann::json parameterToMcpSchema(const cmdsdk::ParameterMetadata& parameter, const nlohmann::json& subtype_options = nullptr) {
  std::string type = "string";  // default
  if (parameter.parameter_type == "number") {
    type = "number";
  } else if (parameter.parameter_type == "boolean") {
    type = "boolean";
  } else if (parameter.parameter_type == "object") {
    type = "object";
  } else if (parameter.parameter_type == "array") {
    type = "array";
  }
  // else string
  nlohmann::json schema = {
      {"type", type},
      {"description", parameter.description}
  };
  
  // Add enum values for subType parameter
  if (parameter.parameter_name == "subType" && subtype_options != nullptr && subtype_options.is_array()) {
    schema["enum"] = subtype_options;
  }
  
  if (!parameter.validation.empty()) {
    // simplistic validation
  }
  return schema;
}

nlohmann::json commandToMcpTool(const cmdsdk::CommandMetadata& metadata) {
  // Extract subType enum values from sub_cmd_types
  nlohmann::json subtype_enums = nlohmann::json::array();
  for (const auto& subtype : metadata.sub_cmd_types) {
    subtype_enums.push_back(subtype.sub_type_name);
  }
  
  nlohmann::json inputSchema = {
      {"type", "object"},
      {"properties", nlohmann::json::object()},
      {"required", nlohmann::json::array()}
  };
  for (const auto& param : metadata.parameters) {
    // Pass subtype_enums only for the subType parameter
    nlohmann::json param_schema = (param.parameter_name == "subType" && !subtype_enums.empty())
                                   ? parameterToMcpSchema(param, subtype_enums)
                                   : parameterToMcpSchema(param);
    inputSchema["properties"][param.parameter_name] = param_schema;
    if (param.required) {
      inputSchema["required"].push_back(param.parameter_name);
    }
  }

  return {
      {"name", metadata.cmd_name},
      {"description", metadata.description},
      {"inputSchema", inputSchema}
  };
}

nlohmann::json makeJsonRpcResult(const nlohmann::json& id, const nlohmann::json& result) {
  return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"result", result},
  };
}

nlohmann::json makeJsonRpcError(const nlohmann::json& id, int code, const std::string& message) {
  return {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error",
       {
           {"code", code},
           {"message", message},
       }},
  };
}

// Helper: Extract plugin name from subtype (format: PLUGIN_NAME.SUBTYPE)
std::string extractPluginName(const std::string& subtype_name) {
  size_t dot_pos = subtype_name.find('.');
  if (dot_pos != std::string::npos) {
    return subtype_name.substr(0, dot_pos);
  }
  return "";
}

// Helper: Build a map of plugins with their subtypes and commands
using PluginInfo = std::map<std::string, std::string>;  // subtype_name -> description
std::map<std::string, PluginInfo> buildPluginRegistry(const cmdsdk::CommandRegistry& registry) {
  std::map<std::string, PluginInfo> plugins;
  
  for (const auto& metadata : registry.listMetadata()) {
    for (const auto& subtype : metadata.sub_cmd_types) {
      std::string plugin_name = extractPluginName(subtype.sub_type_name);
      if (!plugin_name.empty()) {
        plugins[plugin_name][subtype.sub_type_name] = subtype.description;
      }
    }
  }
  
  return plugins;
}

// Helper: Build markdown documentation for all plugins
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

// Helper: Build markdown for a specific plugin
std::string buildPluginDetailsMarkdown(const std::string& plugin_name, const PluginInfo& plugin_info) {
  std::string doc = "# Plugin: " + plugin_name + "\n\n";
  doc += "## Available SubCommand Types\n\n";
  
  for (const auto& [subtype_name, description] : plugin_info) {
    doc += "- **" + subtype_name + "**: " + description + "\n";
  }
  
  return doc;
}

nlohmann::json handleMcpRequest(const nlohmann::json& request, cmdsdk::CommandRegistry& registry) {
  const auto id = request.contains("id") ? request.at("id") : nlohmann::json(nullptr);

  if (!request.is_object()) {
    return makeJsonRpcError(id, -32600, "Invalid Request: body must be an object.");
  }

  if (!request.contains("jsonrpc") || request.at("jsonrpc") != "2.0") {
    return makeJsonRpcError(id, -32600, "Invalid Request: jsonrpc must be 2.0.");
  }

  if (!request.contains("method") || !request.at("method").is_string()) {
    return makeJsonRpcError(id, -32600, "Invalid Request: method must be a string.");
  }

  const auto method = request.at("method").get<std::string>();
  if (method == "initialize") {
    return makeJsonRpcResult(id, {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", {{"listChanged", true}}},
            {"resources", {{"listChanged", true}}}
        }},
        {"serverInfo", {
            {"name", "fastmcp_server"},
            {"version", "0.1.0"}
        }}
    });
  }

  if (method == "tools/list") {
    nlohmann::json tools = nlohmann::json::array();
    for (const auto& command_metadata : registry.listMetadata()) {
      tools.push_back(commandToMcpTool(command_metadata));
    }
    return makeJsonRpcResult(id, {{"tools", tools}});
  }

  if (method == "tools/call") {
    if (!request.contains("params") || !request.at("params").is_object()) {
      return makeJsonRpcError(id, -32602, "Invalid params: params must be an object.");
    }

    const auto& params = request.at("params");
    if (!params.contains("name") || !params.at("name").is_string()) {
      return makeJsonRpcError(id, -32602, "Invalid params: name must be a string.");
    }

    const auto cmd_name = params.at("name").get<std::string>();
    const auto args = params.contains("arguments") ? params.at("arguments") : nlohmann::json::object();
    if (!args.is_object()) {
      return makeJsonRpcError(id, -32602, "Invalid params: arguments must be a JSON object.");
    }

    auto command = registry.create(cmd_name);
    if (!command) {
      return makeJsonRpcError(id, -32601, "Tool not found: " + cmd_name);
    }

    std::string error;
    if (!command->validate(args, error)) {
      return makeJsonRpcError(id, -32000, "Validation failed: " + error);
    }

    if (!command->execute(args, error)) {
      return makeJsonRpcError(id, -32001, "Execution failed: " + error);
    }

    return makeJsonRpcResult(id, {{"content", {{{"type", "text"}, {"text", command->getResult().dump()}} }}});
  }

  if (method == "resources/list") {
    auto plugins = buildPluginRegistry(registry);
    nlohmann::json resources = nlohmann::json::array();
    
    // Add main plugins overview resource
    resources.push_back({
        {"uri", "plugins://overview"},
        {"name", "Plugins Overview"},
        {"description", "Overview of all available plugins and their SubCommand types"},
        {"mimeType", "text/markdown"}
    });
    
    // Add individual plugin resources
    for (const auto& [plugin_name, _] : plugins) {
      resources.push_back({
          {"uri", "plugin://" + plugin_name},
          {"name", "Plugin: " + plugin_name},
          {"description", "Details for " + plugin_name + " plugin including available SubCommand types"},
          {"mimeType", "text/markdown"}
      });
    }
    
    return makeJsonRpcResult(id, {{"resources", resources}});
  }

  if (method == "resources/read") {
    if (!request.contains("params") || !request.at("params").is_object()) {
      return makeJsonRpcError(id, -32602, "Invalid params: params must be an object.");
    }

    const auto& params = request.at("params");
    if (!params.contains("uri") || !params.at("uri").is_string()) {
      return makeJsonRpcError(id, -32602, "Invalid params: uri must be a string.");
    }

    std::string uri = params.at("uri").get<std::string>();
    auto plugins = buildPluginRegistry(registry);

    if (uri == "plugins://overview") {
      std::string content = buildPluginsMarkdown(plugins);
      return makeJsonRpcResult(id, {
          {"contents", {{
              {"uri", uri},
              {"mimeType", "text/markdown"},
              {"text", content}
          }}}
      });
    }

    if (uri.find("plugin://") == 0) {
      std::string plugin_name = uri.substr(9);  // Remove "plugin://" prefix
      auto it = plugins.find(plugin_name);
      if (it != plugins.end()) {
        std::string content = buildPluginDetailsMarkdown(plugin_name, it->second);
        return makeJsonRpcResult(id, {
            {"contents", {{
                {"uri", uri},
                {"mimeType", "text/markdown"},
                {"text", content}
            }}}
        });
      } else {
        return makeJsonRpcError(id, -32001, "Plugin not found: " + plugin_name);
      }
    }

    return makeJsonRpcError(id, -32001, "Resource not found: " + uri);
  }

  return makeJsonRpcError(id, -32601, "Method not found.");
}

std::vector<std::filesystem::path> getAllPluginsInLib(const char* argv0) {
  const auto executable_path = std::filesystem::absolute(argv0);
  const auto executable_directory = executable_path.parent_path();
  const auto lib_path = executable_directory.parent_path() / "lib";

  std::vector<std::filesystem::path> plugins;
  if (std::filesystem::exists(lib_path) && std::filesystem::is_directory(lib_path)) {
    for (const auto& entry : std::filesystem::directory_iterator(lib_path)) {
      if (entry.is_regular_file()) {
        const auto& path = entry.path();
        const auto filename = path.filename().string();
        // Check if it's a shared library (starts with lib and ends with .so on Linux)
        if (filename.starts_with("lib") && filename.ends_with(".so")) {
          plugins.push_back(path);
        }
      }
    }
  }
  return plugins;
}

std::string defaultHtmlPage(int port) {
  return R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <title>FastMCP Command Server</title>
  <style>
    body { font-family: sans-serif; margin: 2rem; line-height: 1.5; }
    code { background: #f2f2f2; padding: 0.2rem 0.4rem; border-radius: 4px; }
  </style>
</head>
<body>
  <h1>FastMCP Command Server</h1>
  <p>Server is running on port <code>)" + std::to_string(port) + R"(</code> with MCP Streamable HTTP enabled.</p>
  <h2>Endpoints</h2>
  <ul>
    <li><code>POST /mcp</code> - MCP JSON-RPC endpoint for tools and resources.</li>
  </ul>
  <h2>MCP Usage Notes</h2>
  <p>Use this server URL: <code>http://localhost:)" + std::to_string(port) + R"(/mcp</code></p>
  <p>Start by calling <code>initialize</code> method, then <code>tools/list</code> to discover commands.</p>
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

  if (!loaded_at_least_one_plugin) {
    std::cerr << "No plugins were loaded. /resources will be empty until commands are registered.\n";
  } else {
    // Log registered commands and subtypes
    std::cout << "Registered commands:\n";
    for (const auto& metadata : registry.listMetadata()) {
      std::cout << "  - " << metadata.cmd_name << ": " << metadata.description << '\n';
      if (!metadata.sub_cmd_types.empty()) {
        std::cout << "    Subtypes:\n";
        for (const auto& subtype : metadata.sub_cmd_types) {
          std::cout << "      - " << subtype.sub_type_name << ": " << subtype.description << '\n';
        }
      }
    }
  }

  httplib::Server server;

  server.Get("/", [&](const httplib::Request&, httplib::Response& response) {
    addCorsHeaders(response);
    response.set_content(defaultHtmlPage(config.port), "text/html");
  });

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
      const auto error = makeJsonRpcError(nullptr, -32700, "Parse error: " + std::string(parse_error.what()));
      response.status = 400;
      response.set_content(error.dump(), "application/json");
      return;
    }

    const auto rpc_response = handleMcpRequest(rpc_request, registry);
    response.set_content(rpc_response.dump(2), "application/json");
  });

  std::cout << "FastMCP server listening on http://0.0.0.0:" << config.port << '\n';
  std::cout << "Endpoints: /, /mcp\n";

  if (!server.listen("0.0.0.0", config.port)) {
    std::cerr << "Failed to bind server on port " << config.port << ".\n";
    return 1;
  }

  return 0;
}
