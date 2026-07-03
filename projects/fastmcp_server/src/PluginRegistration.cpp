// PluginRegistration.cpp
#include "PluginRegistration.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

#include <httplib.h>

#include "FastMcpStringUtils.hpp"

// gMcpDebug is defined in main.cpp; forward-declare here so we can read it.
extern std::atomic_bool gMcpDebug;

namespace fastmcp
{

    using StringUtils = FastMcpStringUtils;

    // ── Shared log helper (duplicated signature; calls the one in main.cpp) ────
    // To avoid a circular dependency we declare a thin forwarding shim.
    // The real logDiag lives in main.cpp (same TU linkage unit after linking).
    static void logDiagLocal(const std::string& area, const std::string& message)
    {
        // Forward to the global logDiag defined in main.cpp.
        // We re-declare it here with external linkage.
        extern void logDiag(const std::string&, const std::string&);
        logDiag(area, message);
    }

    static void logRequestContextLocal(const std::string& area,
                                    const mcp::jsonrpc::RequestContext& rc,
                                    const std::string& method)
    {
        extern void logRequestContext(const std::string&,
                                    const mcp::jsonrpc::RequestContext&,
                                    const std::string&);
        logRequestContext(area, rc, method);
    }

    // ── mcp-apps connectivity ─────────────────────────────────────────────────
    static constexpr const char* kMcpAppsHost = "localhost";
    static constexpr int         kMcpAppsPort = 6543;

    static std::string contentTypeFromResult(const httplib::Result& r, const std::string& fb)
    {
        if (!r || !r->has_header("Content-Type")) return fb;
        std::string v = r->get_header_value("Content-Type");
        const auto sep = v.find(';');
        return (sep != std::string::npos) ? v.substr(0, sep) : (v.empty() ? fb : v);
    }

    // ── JSON bridge helpers ───────────────────────────────────────────────────
    // (local copies; main.cpp has identical ones — keep in sync or move to a shared header)
    using McpJson = mcp::jsonrpc::JsonValue;

    static McpJson toMcpJson(const json& value)
    {
        return McpJson::parse(value.dump());
    }

    static json fromMcpJson(const McpJson& value)
    {
        return json::parse(value.to_string());
    }

    static McpJson makeTextContent(const std::string& text)
    {
        McpJson block = McpJson::object();
        block["type"] = "text";
        block["text"] = text;
        return block;
    }

    // ==========================================================================
    // ToolRegistrationState helpers
    // ==========================================================================

    std::string allocateUniqueToolName(const std::string& base_name,
                                    ToolRegistrationState& state)
    {
        std::string tool_name = base_name;
        if (state.used_tool_names.count(tool_name) > 0)
        {
            int& suffix = state.tool_name_suffixes[tool_name];
            do { ++suffix; }
            while (state.used_tool_names.count(tool_name + "_" + std::to_string(suffix)) > 0);
            tool_name = tool_name + "_" + std::to_string(suffix);
        }
        state.used_tool_names.insert(tool_name);
        return tool_name;
    }

    // ==========================================================================
    // Input schema
    // ==========================================================================

    json buildInputSchema(const cmdsdk::CommandMetadata& cmd_meta,
                        const std::optional<std::vector<std::string>>& fixed_subtype)
    {
        json input_schema = {
            {"type", "object"},
            {"properties", json::object()},
            {"additionalProperties", true}
        };
        std::set<std::string> required_fields;

        for (const auto& param : cmd_meta.parameters)
        {
            std::string json_type = "string";
            if      (param.parameter_type == "number")  json_type = "number";
            else if (param.parameter_type == "boolean") json_type = "boolean";
            else if (param.parameter_type == "object")  json_type = "object";
            else if (param.parameter_type == "array")   json_type = "array";

            input_schema["properties"][param.parameter_name] = {
                {"type", json_type},
                {"description", param.description}
            };
            if (param.required)
                required_fields.insert(param.parameter_name);
        }

        if (fixed_subtype.has_value())
        {
            json enum_values = json::array();
            for (const auto& st : *fixed_subtype)
                enum_values.push_back(st);
            input_schema["properties"]["subType"] = {
                {"type", "string"},
                {"enum", enum_values},
                {"description", "Injected subType for this tool"}
            };
        }
        else if (!cmd_meta.sub_cmd_types.empty())
        {
            json enum_values = json::array();
            for (const auto& st : cmd_meta.sub_cmd_types)
                enum_values.push_back(st.sub_type_name);
            input_schema["properties"]["subType"] = {
                {"type", "string"},
                {"enum", enum_values},
                {"description", "SubCommand type to execute"}
            };
            required_fields.insert("subType");
        }

        if (!required_fields.empty())
        {
            json required = json::array();
            for (const auto& field : required_fields)
                required.push_back(field);
            input_schema["required"] = required;
        }

        return input_schema;
    }

    // ==========================================================================
    // Low-level tool registration
    // ==========================================================================

    bool registerTool(const cmdsdk::CommandRegistry& /*registry*/,
                    mcp::server::Server& server,
                    const std::string& tool_name,
                    const std::string& title,
                    const std::string& description,
                    const json& input_schema,
                    const std::string& original_cmd_name,
                    const std::function<json(const json&)>& handler)
    {
        const bool mcp_debug = gMcpDebug.load();
        const mcp::server::ToolDefinition toolDef{
            .name        = tool_name,
            .title       = title,
            .description = description,
            .inputSchema = toMcpJson(input_schema),
        };

        if (mcp_debug)
            logDiagLocal("MCP-REGISTER",
                "Registering tool " + tool_name + " (command=" + original_cmd_name + ")");

        auto toolHandler = [handler, tool_name, original_cmd_name](
            const mcp::server::ToolCallContext& context) -> mcp::server::CallToolResult
        {
            try
            {
                if (gMcpDebug.load())
                    logRequestContextLocal("MCP-TOOL-CALL", context.requestContext,
                        "tool=" + tool_name + " command=" + original_cmd_name);

                const json args = fromMcpJson(context.arguments);
                const json raw  = handler(args);

                json structured = raw.is_object() ? raw : json{ {"result", raw} };
                if (args.contains("subType") && args["subType"].is_string())
                    structured["subTypeExecuted"] = args["subType"].get<std::string>();

                std::cout << "[tools/call] " << original_cmd_name << " -> " << raw.dump() << '\n';

                mcp::server::CallToolResult result;
                result.structuredContent = toMcpJson(structured);
                result.content           = McpJson::array();
                result.content.push_back(makeTextContent(raw.dump()));
                result.isError           = false;
                return result;
            }
            catch (const std::exception& e)
            {
                if (gMcpDebug.load())
                    logDiagLocal("MCP-TOOL-ERROR",
                        original_cmd_name + " failed: " + e.what());
                throw;
            }
        };

        server.registerTool(toolDef, toolHandler);
        return true;
    }

    // ==========================================================================
    // Command → plain tool
    // ==========================================================================

    bool registerCmdAsTool(const cmdsdk::CommandRegistry& registry,
                        mcp::server::Server& server,
                        const cmdsdk::CommandMetadata& cmd_meta,
                        const std::function<json(const json&)>& handler,
                        ToolRegistrationState& registration_state)
    {
        const std::string original_cmd_name = cmd_meta.cmd_name;

        const auto lambda_register_tool = [&](const std::string& base_tool_name,
                                            const std::string& description,
                                            const json& input_schema,
                                            const std::function<json(const json&)>& wrapped_handler) -> bool
        {
            const auto tool_name = allocateUniqueToolName(base_tool_name, registration_state);
            registration_state.command_tool_names[original_cmd_name].push_back(tool_name);

            std::string tool_description = description;
            if (tool_name != original_cmd_name)
                tool_description += " [original: " + original_cmd_name + "]";

            return registerTool(registry, server,
                                tool_name,
                                original_cmd_name,
                                tool_description,
                                input_schema,
                                original_cmd_name,
                                wrapped_handler);
        };

        return lambda_register_tool(
            StringUtils::sanitizeToolName(original_cmd_name),
            cmd_meta.description,
            buildInputSchema(cmd_meta, std::nullopt),
            handler);
    }

    // ==========================================================================
    // UI-backed tool registration
    // ==========================================================================

    bool registerToolWithUI(mcp::server::Server& server,
                            const std::string& toolName,
                            const std::string& title,
                            const std::string& description,
                            const std::string& resource_uri,
                            const json& input_schema,
                            const std::function<mcp::server::CallToolResult(const json&)>& handler)
    {
        const bool mcp_debug = gMcpDebug.load();

        mcp::server::ToolDefinition toolDef;
        toolDef.name        = toolName;
        toolDef.title       = title;
        toolDef.description = description;
        toolDef.inputSchema = toMcpJson(input_schema);
        toolDef.annotations = mcp::jsonrpc::JsonValue::object({ {"readOnlyHint", true} });
        toolDef.execution   = mcp::jsonrpc::JsonValue::object({ {"taskSupport", "forbidden"} });
        toolDef.metadata    = mcp::jsonrpc::JsonValue::object({
            { "ui", mcp::jsonrpc::JsonValue::object({ {"resourceUri", resource_uri} }) },
            { "ui/resourceUri", resource_uri }
        });

        if (mcp_debug)
            logDiagLocal("MCP-REGISTER",
                "Registering app tool " + toolName + " with UI resource " + resource_uri);

        auto toolHandler = [handler, toolName, resource_uri](
            const mcp::server::ToolCallContext& context) -> mcp::server::CallToolResult
        {
            try
            {
                if (gMcpDebug.load())
                    logRequestContextLocal("MCP-TOOL-CALL", context.requestContext,
                        "tool=" + toolName);

                const mcp::server::CallToolResult result = handler(fromMcpJson(context.arguments));

                if (gMcpDebug.load())
                    logDiagLocal("MCP-TOOL-SUCCESS", toolName + " executed successfully");

                return result;
            }
            catch (const std::exception& e)
            {
                if (gMcpDebug.load())
                    logDiagLocal("MCP-TOOL-ERROR", toolName + " failed: " + e.what());
                throw;
            }
        };

        server.registerTool(std::move(toolDef), toolHandler);

        if (mcp_debug)
            logDiagLocal("MCP-REGISTER",
                "Tool UI resource set to " + resource_uri + " (proxied/app resource)");

        return true;
    }

    bool registerAppToolWithUI(const cmdsdk::CommandRegistry& /*registry*/,
                            mcp::server::Server& server,
                            const cmdsdk::CommandMetadata& cmd_meta,
                            ToolRegistrationState& registration_state)
    {
        const bool mcp_debug = gMcpDebug.load();
        const std::string original_cmd_name = cmd_meta.cmd_name;
        const std::string tool_base_name =
            "open-" + StringUtils::sanitizeToolName(original_cmd_name) + "-form";
        const std::string tool_name  = allocateUniqueToolName(tool_base_name, registration_state);
        const std::string title       = "Open " + original_cmd_name + " Form";
        const std::string description = "Open a UI form for " + original_cmd_name + ".";
        const json        input_schema = buildInputSchema(cmd_meta, std::nullopt);

        std::vector<std::string>         subtype_names;
        std::map<std::string, std::string> subtype_labels;
        for (const auto& st : cmd_meta.sub_cmd_types)
        {
            subtype_names.push_back(st.sub_type_name);
            subtype_labels[st.sub_type_name] = st.description;
        }

        const std::string resolved_resource_uri = cmd_meta.resource_uri.empty()
            ? "ui://ui/" + StringUtils::sanitizeToolName(original_cmd_name) + "-form.html"
            : cmd_meta.resource_uri;
        const std::string command_tool_name =
            (!registration_state.command_tool_names[original_cmd_name].empty())
                ? registration_state.command_tool_names[original_cmd_name].front()
                : std::string();

        if (mcp_debug)
            logDiagLocal("MCP-REGISTER",
                "App tool registration for " + original_cmd_name +
                ": tool=" + tool_name +
                ", resourceUri=" + resolved_resource_uri +
                ", metadata.resource_uri=" + (cmd_meta.resource_uri.empty() ? "<none>" : cmd_meta.resource_uri));

        auto ui_handler = [resolved_resource_uri, command_tool_name, original_cmd_name,
                        subtype_names, subtype_labels](const json& args)
            -> mcp::server::CallToolResult
        {
            mcp::server::CallToolResult result;
            json response = {
                {"status",       "success"},
                {"availability", original_cmd_name + " form available"},
                {"message",      "UI form available"},
                {"resourceUri",  resolved_resource_uri},
                {"toolName",     command_tool_name},
                {"commandName",  original_cmd_name},
                {"subTypes",     subtype_names},
                {"labels",       subtype_labels}
            };
            if (!args.is_null() && !args.empty())
                response["args"] = args;

            result.structuredContent = toMcpJson(response);
            result.content           = McpJson::array();
            result.content.push_back(makeTextContent(response.dump()));
            result.isError           = false;
            return result;
        };

        return registerToolWithUI(server, tool_name, title, description,
                                resolved_resource_uri, input_schema, ui_handler);
    }

    // ==========================================================================
    // Plugin file discovery
    // ==========================================================================

    #if defined(_WIN32)
    static const std::set<std::string> kNonPluginLibs = { "cmd_sdk.dll" };
    #elif defined(__APPLE__)
    static const std::set<std::string> kNonPluginLibs = { "libcmd_sdk.dylib" };
    #else
    static const std::set<std::string> kNonPluginLibs = { "libcmd_sdk.so" };
    #endif

    std::vector<std::filesystem::path> getAllPluginsInLib(const char* argv0)
    {
        const auto exe    = std::filesystem::absolute(argv0);
        const auto exeDir = exe.parent_path();
        const auto parent = exeDir.parent_path();

        std::vector<std::filesystem::path> dirs = { exeDir };
        if (!parent.empty())
        {
            dirs.push_back(parent);
            dirs.push_back(parent / "lib");
            const auto buildRoot = parent.parent_path();
            if (!buildRoot.empty())
            {
                dirs.push_back(buildRoot / "lib");
                dirs.push_back(buildRoot / "bin");
                const auto cfg = exeDir.filename().string();
                if (cfg == "Debug" || cfg == "Release" ||
                    cfg == "RelWithDebInfo" || cfg == "MinSizeRel")
                {
                    dirs.push_back(buildRoot / "lib" / cfg);
                    dirs.push_back(buildRoot / "bin" / cfg);
                }
            }
        }

        auto isPlugin = [](const std::filesystem::path& p) -> bool
        {
            if (!p.has_filename()) return false;
            const auto fn = p.filename().string();
    #if defined(_WIN32)
            return p.extension() == ".dll";
    #elif defined(__APPLE__)
            return StringUtils::beginsWith(fn, "lib") && p.extension() == ".dylib";
    #else
            return StringUtils::beginsWith(fn, "lib") && p.extension() == ".so";
    #endif
        };

        std::set<std::string>             seen;
        std::vector<std::filesystem::path> plugins;

        for (const auto& dir : dirs)
        {
            if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) continue;
            std::vector<std::filesystem::path> entries;
            for (const auto& e : std::filesystem::directory_iterator(dir))
            {
                if (!e.is_regular_file() || !isPlugin(e.path())) continue;
                const auto fn = e.path().filename().string();
                if (kNonPluginLibs.count(fn))
                {
                    std::cout << "Skipping SDK lib: " << fn << '\n';
                    continue;
                }
                entries.push_back(std::filesystem::absolute(e.path()));
            }
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b)
            {
                const auto an = a.filename().string(), bn = b.filename().string();
                return an != bn ? an < bn : a.string() < b.string();
            });
            for (const auto& p : entries)
            {
                std::string fn = p.filename().string();
    #if defined(_WIN32)
                fn = StringUtils::toUpperAscii(fn);
    #endif
                if (!seen.insert(fn).second)
                {
                    std::cout << "Skipping duplicate: " << p.filename().string() << '\n';
                    continue;
                }
                plugins.push_back(p);
            }
        }
        return plugins;
    }

    // ==========================================================================
    // Plugin name helpers
    // ==========================================================================

    std::string canonicalResourceName(const std::string& plugin_name)
    {
        const auto u = StringUtils::toUpperAscii(plugin_name);
        if (u == "GEO")  return "geometry";
        if (u == "MATH") return "math";
        return StringUtils::toLowerAscii(plugin_name);
    }

    std::string resolvePluginName(const cmdsdk::CommandMetadata& m)
    {
        if (!m.plugin_name.empty()) return m.plugin_name;
        if (!m.sub_cmd_types.empty())
        {
            const auto& f = m.sub_cmd_types.front().sub_type_name;
            auto dot = f.find('.');
            return (dot != std::string::npos) ? f.substr(0, dot) : f;
        }
        return m.cmd_name;
    }

    std::string openApiPathForCommand(const cmdsdk::CommandMetadata& m)
    {
        return "/api/" + m.cmd_name;
    }

    // ==========================================================================
    // OpenAPI helpers
    // ==========================================================================

    bool mergeMissingSubtypeEnumsIntoSpec(json& spec, const cmdsdk::CommandMetadata& meta)
    {
        if (!spec.is_object()) return false;
        const auto path = openApiPathForCommand(meta);
        try
        {
            auto& sub_type_schema =
                spec["paths"][path]["post"]["requestBody"]["content"]
                    ["application/json"]["schema"]["properties"]["subType"];
            if (!sub_type_schema.contains("enum") || !sub_type_schema["enum"].is_array())
                sub_type_schema["enum"] = json::array();
            std::set<std::string> known;
            for (const auto& v : sub_type_schema["enum"])
                if (v.is_string()) known.insert(v.get<std::string>());
            bool changed = false;
            for (const auto& st : meta.sub_cmd_types)
                if (known.insert(st.sub_type_name).second)
                {
                    sub_type_schema["enum"].push_back(st.sub_type_name);
                    changed = true;
                }
            return changed;
        }
        catch (...) { return false; }
    }

    // ==========================================================================
    // Plugin registry
    // ==========================================================================

    std::map<std::string, PluginInfo> buildPluginRegistry(const cmdsdk::CommandRegistry& reg)
    {
        std::map<std::string, PluginInfo> plugins;
        for (const auto& meta : reg.listMetadata())
            for (const auto& st : meta.sub_cmd_types)
                plugins[resolvePluginName(meta)][st.sub_type_name] = st;
        return plugins;
    }

    const PluginInfo* findPluginInfo(const std::map<std::string, PluginInfo>& plugins,
                                    const std::string& requested,
                                    std::string& resolved)
    {
        if (const auto it = plugins.find(requested); it != plugins.end())
        {
            resolved = it->first; return &it->second;
        }
        const auto ru = StringUtils::toUpperAscii(requested);
        for (const auto& [pn, pi] : plugins)
            if (StringUtils::toUpperAscii(pn) == ru) { resolved = pn; return &pi; }
        const auto rl = StringUtils::toLowerAscii(requested);
        for (const auto& [pn, pi] : plugins)
        {
            const auto pu = StringUtils::toUpperAscii(pn);
            if (((rl == "geo" || rl == "geometry") && pu == "GEO") ||
                (rl == "math" && pu == "MATH") ||
                canonicalResourceName(pn) == rl)
            {
                resolved = pn; return &pi;
            }
        }
        return nullptr;
    }

    // ==========================================================================
    // External mcp-apps resource discovery & reading
    // ==========================================================================

    std::vector<ExternalResourceInfo> fetchExternalAppResources()
    {
        std::vector<ExternalResourceInfo> res;
        httplib::Client cl(kMcpAppsHost, kMcpAppsPort);
        cl.set_connection_timeout(2, 0);
        cl.set_read_timeout(2, 0);
        const auto r = cl.Get("/resource-manifest.json");
        if (!r || r->status != 200) return res;
        try
        {
            const auto manifest = json::parse(r->body);
            if (!manifest.is_object() || !manifest.contains("resources")) return res;
            for (const auto& e : manifest["resources"])
            {
                if (!e.is_object() || !e.contains("uri")) continue;
                const std::string ruri = e["uri"].get<std::string>();
                std::string app_path;
                if (!StringUtils::uriToMcpAppsPath(ruri, app_path)) continue;
                ExternalResourceInfo info;
                info.uri         = StringUtils::toMcpAppUri(ruri);
                info.name        = e.value("name", "App Resource: " + app_path);
                info.description = e.value("description", "Resource from local mcp-apps");
                info.mime_type   = e.value("mimeType", "application/json");
                if (e.contains("_meta") && e["_meta"].is_object()) info.meta = e["_meta"];
                res.push_back(std::move(info));
            }
        }
        catch (...) {}
        return res;
    }

    bool readMcpAppResource(const std::string& uri,
                            std::string& canonical_uri,
                            std::string& mime_type,
                            std::string& content,
                            std::string& error)
    {
        const bool mcp_debug = gMcpDebug.load();
        std::string path;
        if (!StringUtils::uriToMcpAppsPath(uri, path))
        {
            if (mcp_debug)
                logDiagLocal("MCP-APP-READ", "Rejected URI mapping for " + uri);
            error = "Invalid URI scheme mapping";
            return false;
        }
        if (path.empty() || path[0] != '/')
            path = "/" + path;

        httplib::Client cl(kMcpAppsHost, kMcpAppsPort);
        cl.set_connection_timeout(1, 0);
        cl.set_read_timeout(2, 0);

        if (mcp_debug)
            logDiagLocal("MCP-APP-READ", "Fetching " + uri + " via " + path);

        const auto r = cl.Get(path.c_str());
        if (!r)
        {
            if (mcp_debug)
                logDiagLocal("MCP-APP-READ", "Connection failed for " + uri);
            error = "mcp-apps connection failed at " + std::string(kMcpAppsHost);
            return false;
        }
        if (r->status != 200)
        {
            if (mcp_debug)
                logDiagLocal("MCP-APP-READ",
                    "Resource not found for " + uri + ": HTTP " + std::to_string(r->status));
            error = (r->status == 404)
                ? "Resource not found: " + uri
                : "mcp-apps error: " + std::to_string(r->status);
            return false;
        }

        canonical_uri = uri;
        mime_type     = contentTypeFromResult(r, "text/html");
        content       = r->body;

        if (mcp_debug)
            logDiagLocal("MCP-APP-READ", "Success for " + uri + " mime=" + mime_type);

        return true;
    }

    // ==========================================================================
    // OpenAPI aggregation pass
    // ==========================================================================

    void buildOpenApiAggregator(const cmdsdk::CommandRegistry& registry,
                                cmdsdk::OpenApiAggregator& api_aggregator)
    {
        const auto& pm_registry = cmdsdk::PluginMetadataRegistry::instance();
        std::map<std::string, bool> processed_plugins;

        if (registry.listMetadata().empty()) return;

        std::cout << "\nRegistered commands:\n";
        for (const auto& meta : registry.listMetadata())
        {
            const auto pname    = resolvePluginName(meta);
            const bool has_custom = pm_registry.hasCustomOpenApi(pname);
            const auto cmd_path = openApiPathForCommand(meta);

            if (!processed_plugins[pname])
            {
                processed_plugins[pname] = true;
                if (has_custom)
                {
                    api_aggregator.addPluginSpec(pname,
                        pm_registry.getCustomOpenApi(pname),
                        "registered-metadata");
                    std::cout << "    [OpenAPI] Custom spec loaded\n";
                }
            }

            if (has_custom)
            {
                auto pspec = api_aggregator.getPluginSpec(pname);
                if (!pspec.contains("paths") || !pspec["paths"].contains(cmd_path))
                    api_aggregator.addAutoGeneratedSpec(meta, pname);
                else if (mergeMissingSubtypeEnumsIntoSpec(pspec, meta))
                    api_aggregator.addPluginSpec(pname, pspec, "registered-metadata+provider");
            }
            else
            {
                api_aggregator.addAutoGeneratedSpec(meta, pname);
            }

            std::cout << "  - [" << pname << "] " << meta.cmd_name
                    << ": " << meta.description << '\n';
            for (const auto& st : meta.sub_cmd_types)
                std::cout << "      - " << st.sub_type_name << ": " << st.description << '\n';
        }
    }

} // namespace fastmcp