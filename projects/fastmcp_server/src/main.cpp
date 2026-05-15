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
// Auxiliary REST layer → cpp-httplib (kept for non-MCP routes)
//   GET  /             — homepage
//   GET  /swagger      — Swagger UI
//   GET  /openapi.json — combined OpenAPI spec
//   GET  /openapi.yaml — combined OpenAPI spec (YAML)
//   GET  /openapi/:p   — per-plugin spec
//   POST /api/:cmd     — REST command execution
//   GET  /mcp-apps*    — reverse-proxy to mcp-apps at :6543
//
// Port layout:
//   MCP  → port N   (e.g. 5432) — cpp-mcp-sdk streamable HTTP runner
//   REST → port N+1 (e.g. 5433) — httplib on a detached thread
// ---------------------------------------------------------------------------

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <map>
#include <algorithm>
#include <cctype>
#include <thread>
#include <memory>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <optional>
#include <mutex>
#include <iomanip>
#include <cstdlib>

// ── cpp-mcp-sdk ───────────────────────────────────────────────────────────
#include <mcp/lifecycle/session/implementation.hpp>
#include <mcp/lifecycle/session/resources_capability.hpp>
#include <mcp/lifecycle/session/server_capabilities.hpp>
#include <mcp/lifecycle/session/tools_capability.hpp>
#include <mcp/server/all.hpp>

// ── Auxiliary HTTP for non-MCP routes ────────────────────────────────────
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

namespace
{

    using json = nlohmann::json;

    using McpJson = mcp::jsonrpc::JsonValue;

    constexpr int kDefaultServerPort = CMDSDK_SERVER_PORT;

#if defined(_WIN32)
    static const std::set<std::string> kNonPluginLibs = { "cmd_sdk.dll" };
#elif defined(__APPLE__)
    static const std::set<std::string> kNonPluginLibs = { "libcmd_sdk.dylib" };
#else
    static const std::set<std::string> kNonPluginLibs = { "libcmd_sdk.so" };
#endif

    enum class ProtocolMode { MCP_ONLY, REST_ONLY, ALL };

    struct ServerConfig
    {
        int port = kDefaultServerPort;
        std::vector<std::filesystem::path> plugin_paths;
        ProtocolMode protocol_mode = ProtocolMode::ALL;
    };

    std::atomic_bool gStopRequested{ false };
    std::atomic_uint64_t gMcpServerInstanceCounter{ 0 };
    std::mutex gLogMutex;

    std::string nowUtcIso8601()
    {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    std::string safeSessionId(const std::optional<std::string>& session_id)
    {
        return session_id.has_value() ? *session_id : "<none>";
    }

    void logDiag(const std::string& area, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(gLogMutex);
        std::cout << "[" << nowUtcIso8601() << "] [" << area << "] " << message << '\n';
    }

    std::string currentExceptionMessage()
    {
        try
        {
            throw;
        }
        catch (const std::exception& ex)
        {
            return ex.what();
        }
        catch (...)
        {
            return "non-std exception";
        }
    }

    void logRequestContext(const std::string& area,
        const mcp::jsonrpc::RequestContext& request_context,
        const std::string& method_or_target)
    {
        logDiag(area,
            method_or_target +
            " session=" + safeSessionId(request_context.sessionId) +
            " protocol=" + request_context.protocolVersion);
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

    void handleSignal(int)
    {
        gStopRequested.store(true);
    }

    McpJson toMcpJson(const json& value)
    {
        return McpJson::parse(value.dump());
    }

    json fromMcpJson(const McpJson& value)
    {
        return json::parse(value.to_string());
    }

    McpJson makeTextContent(const std::string& text)
    {
        McpJson block = McpJson::object();
        block["type"] = "text";
        block["text"] = text;
        return block;
    }

    McpJson makeResourceLinkContent(const std::string& uri,
        const std::string& name,
        const std::string& description,
        const std::string& mime_type)
    {
        McpJson block = McpJson::object();
        block["type"] = "resource_link";
        block["uri"] = uri;
        block["name"] = name;
        block["description"] = description;
        block["mimeType"] = mime_type;
        return block;
    }

    bool registerTool(mcp::server::Server& server,
        const std::string& toolName,
        const std::string& title,
        const std::string& description,
        const json& input_schema)
    {
        const mcp::server::ToolDefinition toolDef{
            .name = toolName,
            .title = title,
            .description = description,
            .inputSchema = toMcpJson(input_schema),
        };

        // server->registerTool(
        //     std::move(toolDef),
        //     [handler](const json& input)
        //     {
        //         try
        //         {
        //             return handler(input);
        //         }
        //         catch (const std::exception& ex)
        //         {
        //             logDiag("ToolHandler", std::string("Exception: ") + ex.what());
        //             mcp::server::CallToolResult result;
        //             result.isError = true;
        //             result.content = makeTextContent(std::string("Error: ") + ex.what());
        //             return result;
        //         }
        //         catch (...)
        //         {
        //             logDiag("ToolHandler", "Unknown exception");
        //             mcp::server::CallToolResult result;
        //             result.isError = true;
        //             result.content = makeTextContent("Error: unknown exception");
        //             return result;
        //         }
        //     });
        return true;
    }

    bool registerToolWithUI(
        mcp::server::Server& server,
        const std::string& toolName,
        const std::string& title,
        const std::string& description,
        const std::string& resourceFileName,
        const json& input_schema,
        const std::function<mcp::server::CallToolResult(const json&)>& handler)
    {
        const std::string resourceUri = "ui://" + toolName + "/" + resourceFileName;
        return true;
    }

    // ── small string helpers ──────────────────────────────────────────────────
    bool beginsWith(const std::string& v, const std::string& p)
    {
        return v.size() >= p.size() && v.compare(0, p.size(), p) == 0;
    }
    bool endsWith(const std::string& v, const std::string& s)
    {
        return v.size() >= s.size() && v.compare(v.size() - s.size(), s.size(), s) == 0;
    }
    std::string toUpperAscii(std::string v)
    {
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return v;
    }
    std::string toLowerAscii(std::string v)
    {
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    }

    std::string sanitizeToolName(std::string name)
    {
        std::string out;
        out.reserve(name.size());

        bool last_was_sep = false;
        for (const unsigned char c : name)
        {
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            {
                out.push_back(static_cast<char>(c));
                last_was_sep = false;
                continue;
            }

            if (c >= 'A' && c <= 'Z')
            {
                out.push_back(static_cast<char>(std::tolower(c)));
                last_was_sep = false;
                continue;
            }

            if (c == '.' || c == '/' || std::isspace(c))
            {
                if (!last_was_sep && !out.empty())
                {
                    out.push_back('_');
                    last_was_sep = true;
                }
            }
        }

        while (!out.empty() && (out.back() == '_' || out.back() == '-')) out.pop_back();
        if (out.empty()) return "tool";
        return out;
    }

    // ── argument parsing ──────────────────────────────────────────────────────
    ServerConfig parseArguments(int argc, char** argv)
    {
        ServerConfig cfg;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (beginsWith(arg, "--port="))
            {
                cfg.port = std::stoi(arg.substr(7));
            }
            else if (beginsWith(arg, "--plugins="))
            {
                std::stringstream ss(arg.substr(10));
                std::string p;
                while (std::getline(ss, p, ','))
                    if (!p.empty()) cfg.plugin_paths.emplace_back(p);
            }
            else if (beginsWith(arg, "--protocol="))
            {
                std::string m = arg.substr(11);
                if (m == "mcp")       cfg.protocol_mode = ProtocolMode::MCP_ONLY;
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

    // ── plugin registry helpers ───────────────────────────────────────────────
    using PluginInfo = std::map<std::string, cmdsdk::SubCmdTypeMetadata>;

    std::string canonicalResourceName(const std::string& plugin_name)
    {
        const auto u = toUpperAscii(plugin_name);
        if (u == "GEO")  return "geometry";
        if (u == "MATH") return "math";
        return toLowerAscii(plugin_name);
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

    std::map<std::string, PluginInfo> buildPluginRegistry(const cmdsdk::CommandRegistry& reg)
    {
        std::map<std::string, PluginInfo> plugins;
        for (const auto& meta : reg.listMetadata())
            for (const auto& st : meta.sub_cmd_types)
                plugins[resolvePluginName(meta)][st.sub_type_name] = st;
        return plugins;
    }

    // ── OpenAPI helpers ───────────────────────────────────────────────────────
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

    // ── Plugin discovery ──────────────────────────────────────────────────────
    std::vector<std::filesystem::path> getAllPluginsInLib(const char* argv0)
    {
        const auto exe = std::filesystem::absolute(argv0);
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
                return beginsWith(fn, "lib") && p.extension() == ".dylib";
#else
                return beginsWith(fn, "lib") && p.extension() == ".so";
#endif
            };

        std::set<std::string> seen;
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
                fn = toUpperAscii(fn);
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

    // ── mcp-apps reverse-proxy helpers ────────────────────────────────────────
    constexpr const char* kMcpAppsHost = "localhost";
    constexpr int         kMcpAppsPort = 6543;
    constexpr const char* kMathFormPath = "/ui/math-form.html";

    std::string contentTypeFromResponse(const httplib::Result& r, const std::string& fb)
    {
        if (!r || !r->has_header("Content-Type")) return fb;
        std::string v = r->get_header_value("Content-Type");
        const auto sep = v.find(';');
        return (sep != std::string::npos) ? v.substr(0, sep) : (v.empty() ? fb : v);
    }

    bool uriToMcpAppsPath(const std::string& uri, std::string& path)
    {
        if (uri.empty()) return false;
        if (uri == "app://math-form") { path = kMathFormPath; return true; }
        if (uri == "app://dashboard-ui") { path = "/"; return true; }
        if (beginsWith(uri, "app://"))
        {
            const std::string s = uri.substr(6);
            path = (s.empty() || s[0] != '/') ? "/" + s : s;
            return true;
        }
        const std::string pfx = "http://localhost:6543";
        if (beginsWith(uri, pfx))
        {
            const std::string s = uri.substr(pfx.size());
            path = s.empty() ? "/" : s;
            return true;
        }
        if (!uri.empty() && uri[0] == '/') { path = uri; return true; }
        return false;
    }

    std::string toMcpAppUri(const std::string& uri)
    {
        std::string path;
        if (!uriToMcpAppsPath(uri, path)) return uri;
        if (path == kMathFormPath) return "app://math-form";
        return (path == "/") ? "app://dashboard-ui" : "app://" + path.substr(1);
    }

    struct ExternalResourceInfo
    {
        std::string uri, name, description, mime_type;
        json meta;
    };

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
                if (!uriToMcpAppsPath(ruri, app_path)) continue;
                ExternalResourceInfo info;
                info.uri = toMcpAppUri(ruri);
                info.name = e.value("name", "App Resource: " + app_path);
                info.description = e.value("description", "Resource from local mcp-apps");
                info.mime_type = e.value("mimeType", "application/json");
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
        const bool mcp_debug = envFlagEnabled("FASTMCP_MCP_DEBUG", false);
        std::string path;
        if (!uriToMcpAppsPath(uri, path))
        {
            if (mcp_debug)
            {
                logDiag("MCP-APP-READ", "Rejected URI mapping for " + uri);
            }
            error = "Invalid URI scheme mapping";
            return false;
        }

        // Ensure path starts with / for httplib
        if (path.empty() || path[0] != '/')
        {
            path = "/" + path;
        }

        httplib::Client cl(kMcpAppsHost, kMcpAppsPort);
        cl.set_connection_timeout(1, 0); // 1s is plenty for localhost
        cl.set_read_timeout(2, 0);

        if (mcp_debug)
        {
            logDiag("MCP-APP-READ", "Fetching " + uri + " via " + path);
        }

        const auto r = cl.Get(path.c_str());

        if (!r)
        {
            if (mcp_debug)
            {
                logDiag("MCP-APP-READ", "Connection failed for " + uri);
            }
            error = "mcp-apps connection failed at " + std::string(kMcpAppsHost);
            return false;
        }

        if (r->status != 200)
        {
            if (mcp_debug)
            {
                logDiag("MCP-APP-READ", "Non-200 for " + uri + ": " + std::to_string(r->status));
            }
            error = "mcp-apps error: " + std::to_string(r->status);
            return false;
        }

        // Echo back the caller URI; resource handlers currently publish their registered URI.
        canonical_uri = uri;

        // Force HTML if we are proxying a Dashboard UI
        mime_type = contentTypeFromResponse(r, "text/html");
        content = r->body;

        if (mcp_debug)
        {
            logDiag("MCP-APP-READ", "Success for " + uri + " mime=" + mime_type);
        }

        return true;
    }

    // ── Plugin markdown builders ──────────────────────────────────────────────
    std::string buildPluginsMarkdown(const std::map<std::string, PluginInfo>& plugins)
    {
        std::string doc = "# Available Plugins and SubCommand Types\n\n";
        for (const auto& [pname, subtypes] : plugins)
        {
            doc += "## Plugin: " + pname + "\n\n### Available SubCommand Types:\n\n";
            for (const auto& [sname, sm] : subtypes)
            {
                doc += "- **" + sname + "**: " + sm.description + "\n";
                if (sm.response_schema.is_object() && !sm.response_schema.empty())
                    doc += "  - Response schema:\n```json\n" + sm.response_schema.dump(2) + "\n```\n";
            }
            doc += "\n";
        }
        return doc;
    }

    std::string buildPluginDetailsMarkdown(const std::string& pname, const PluginInfo& pi)
    {
        std::string doc = "# Plugin: " + pname + "\n\n## Available SubCommand Types\n\n";
        for (const auto& [sname, sm] : pi)
        {
            doc += "- **" + sname + "**: " + sm.description + "\n";
            if (sm.response_schema.is_object() && !sm.response_schema.empty())
                doc += "  - Response schema:\n```json\n" + sm.response_schema.dump(2) + "\n```\n";
        }
        return doc;
    }

    const PluginInfo* findPluginInfo(const std::map<std::string, PluginInfo>& plugins,
        const std::string& requested, std::string& resolved)
    {
        if (const auto it = plugins.find(requested); it != plugins.end())
        {
            resolved = it->first; return &it->second;
        }
        const auto ru = toUpperAscii(requested);
        for (const auto& [pn, pi] : plugins)
            if (toUpperAscii(pn) == ru) { resolved = pn; return &pi; }
        const auto rl = toLowerAscii(requested);
        for (const auto& [pn, pi] : plugins)
        {
            const auto pu = toUpperAscii(pn);
            if (((rl == "geo" || rl == "geometry") && pu == "GEO") ||
                (rl == "math" && pu == "MATH") ||
                canonicalResourceName(pn) == rl)
            {
                resolved = pn; return &pi;
            }
        }
        return nullptr;
    }

    // ── CORS helper ───────────────────────────────────────────────────────────
    void addCorsHeaders(httplib::Response& res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, MCP-Session-Id, MCP-Protocol-Version");
    }

    // ── Homepage ──────────────────────────────────────────────────────────────
    std::string defaultHtmlPage(int mcp_port, int rest_port)
    {
        return R"HTML(<!DOCTYPE html>
    <html lang="en">
    <head>
    <meta charset="UTF-8">
    <title>FastMCP Server</title>
    <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0-alpha1/dist/css/bootstrap.min.css" rel="stylesheet">
    <style>
        body  { background:#0a0f1a; color:#eef2ff; font-family:system-ui,sans-serif; padding:2rem; }
        .card { background:rgba(18,24,34,.9); border:1px solid rgba(0,150,255,.25);
                border-radius:14px; padding:1.5rem; transition:border-color .2s; }
        .card:hover { border-color:rgba(0,150,255,.55); }
        code  { background:#0b0f16; color:#bbd9ff; padding:.2rem .5rem; border-radius:6px; }
        h1 span { color:#7bb8ff; }
        .badge-live { background:#0e3a1e; color:#4cff8a; border:1px solid #1a6630;
                    border-radius:20px; padding:4px 12px; font-size:.8rem; }
    </style>
    </head>
    <body>
    <div class="container">
    <h1 class="mb-1">🔌 FastMCP <span>Command Server</span></h1>
        <p class="text-secondary mb-4">Powered by <strong>cpp-mcp-sdk</strong> (itcv-GmbH) · MCP 2025-11-25 · C++17</p>

    <div class="row g-3 mb-4">
        <div class="col-md-6">
        <div class="card">
            <div class="d-flex align-items-center gap-2 mb-2">
            <h5 class="mb-0">MCP Streamable HTTP</h5>
            <span class="badge-live ms-auto">LIVE</span>
            </div>
            <p class="text-secondary small mb-2">JSON-RPC 2.0 — tools, resources, SSE transport</p>
            <code>POST http://localhost:)HTML" + std::to_string(mcp_port) + R"HTML(/mcp</code>
            <ul class="list-unstyled small mt-3 mb-0" style="opacity:.85">
            <li>→ <code>initialize</code></li>
            <li>→ <code>tools/list</code> &amp; <code>tools/call</code></li>
            <li>→ <code>resources/list</code> &amp; <code>resources/read</code></li>
            </ul>
        </div>
        </div>
        <div class="col-md-6">
        <div class="card">
            <h5 class="mb-2">REST &amp; Swagger</h5>
            <p class="text-secondary small mb-2">Port )HTML" + std::to_string(rest_port) + R"HTML(</p>
            <ul class="list-unstyled small mb-0">
            <li><code>GET  /swagger</code> — Interactive UI</li>
            <li><code>GET  /openapi.json</code> — OpenAPI spec</li>
            <li><code>GET  /openapi.yaml</code> — YAML spec</li>
            <li><code>POST /api/{command}</code> — REST execution</li>
            <li><code>GET  /mcp-apps/*</code> — App proxy (:6543)</li>
            </ul>
        </div>
        </div>
    </div>
    <p class="text-secondary small">MCP port <strong>)HTML" + std::to_string(mcp_port) + R"HTML(</strong>
        &nbsp;·&nbsp; REST port <strong>)HTML" + std::to_string(rest_port) + R"HTML(</strong>
        &nbsp;·&nbsp; cpp-mcp-sdk library (itcv-GmbH/cpp-mcp-sdk)</p>
    </div>
    </body>
    </html>)HTML";
    }

    std::string protocolModeToString(ProtocolMode m)
    {
        switch (m)
        {
        case ProtocolMode::MCP_ONLY:  return "MCP Only";
        case ProtocolMode::REST_ONLY: return "REST Only";
        default:                       return "MCP + REST";
        }
    }

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
            {
                logDiag("MCP-RESOURCE", "Skipping duplicate resource URI " + resource.uri);
            }
            return false;
        }

        resources.push_back(std::move(resource));
        return true;
    }

} // namespace

// ===========================================================================
// main
// ===========================================================================
int main(int argc, char** argv)
{
    ServerConfig config = parseArguments(argc, argv);
    const bool mcp_debug = envFlagEnabled("FASTMCP_MCP_DEBUG", false);
    const bool require_session_id = envFlagEnabled("FASTMCP_REQUIRE_SESSION_ID", true);

    if (config.plugin_paths.empty())
        config.plugin_paths = getAllPluginsInLib(argv[0]);

    // ── Load plugins ──────────────────────────────────────────────────────
    cmdsdk::CommandRegistry registry;
    PluginLoader loader;
    cmdsdk::OpenApiAggregator api_aggregator;
    bool loaded_at_least_one = false;

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

    // ── Build OpenAPI aggregator ──────────────────────────────────────────
    std::map<std::string, bool> processed_plugins;
    const auto& pm_registry = cmdsdk::PluginMetadataRegistry::instance();

    if (!registry.listMetadata().empty())
    {
        std::cout << "\nRegistered commands:\n";
        for (const auto& meta : registry.listMetadata())
        {
            const auto pname = resolvePluginName(meta);
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
                {
                    api_aggregator.addAutoGeneratedSpec(meta, pname);
                }
                else if (mergeMissingSubtypeEnumsIntoSpec(pspec, meta))
                {
                    api_aggregator.addPluginSpec(pname, pspec, "registered-metadata+provider");
                }
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

    if (!loaded_at_least_one)
        std::cerr << "Warning: no plugins loaded.\n";

    auto plugins = buildPluginRegistry(registry);

    // ── Build MCP resources (plugins + apps) ─────────────────────────────
    std::vector<RuntimeResource> resources;
    std::set<std::string> seen_resource_uris;
    addRuntimeResource(resources, seen_resource_uris, RuntimeResource{
        "plugins://overview",
        "plugins-overview",
        "Available plugins and subtype docs",
        "text/markdown",
        [&plugins]() { return buildPluginsMarkdown(plugins); }
        }, mcp_debug);

    for (const auto& [pname, _] : plugins)
    {
        const auto rname = canonicalResourceName(pname);
        const auto uri = "plugin://" + rname;
        addRuntimeResource(resources, seen_resource_uris, RuntimeResource{
            uri,
            rname,
            "Plugin details for " + pname,
            "text/markdown",
            [&plugins, pname]()
 {
std::string resolved;
const auto* pi = findPluginInfo(plugins, pname, resolved);
if (!pi) return std::string("Plugin not found: " + pname);
return buildPluginDetailsMarkdown(resolved, *pi);
}
            }, mcp_debug);
    }

    // Add apps overview resource
    addRuntimeResource(resources, seen_resource_uris, RuntimeResource{
        "apps://overview",
        "apps-overview",
        "Available MCP apps and UI resources from local mcp-apps service",
        "text/markdown",
        []()
 {
std::string doc = "# Available MCP Apps\n\n";
doc += "Math Form: [app://math-form](app://math-form)\n\n";
doc += "Access the form at http://localhost:6543/ui/math-form.html\n";
return doc;
}
        }, mcp_debug);

    addRuntimeResource(resources, seen_resource_uris, RuntimeResource{
        "app://math-form",
        "math-form",
        "Math form HTML proxied from local mcp-apps server",
        "text/html",
        []()
 {
std::string canon, mime, body, err;
if (readMcpAppResource("app://math-form", canon, mime, body, err))
    return body;
return std::string("<h1>Math Form</h1><p>mcp-apps not reachable at localhost:6543</p>");
}
        }, mcp_debug);

    for (const auto& ext : fetchExternalAppResources())
    {
        addRuntimeResource(resources, seen_resource_uris, RuntimeResource{
            ext.uri,
            ext.name,
            ext.description,
            ext.mime_type,
            [uri = ext.uri]()
 {
std::string canon, mime, body, err;
if (readMcpAppResource(uri, canon, mime, body, err))
    return body;
return std::string("Error: " + err);
}
            }, mcp_debug);
    }

    auto createConfiguredMcpServer = [&registry, &resources, mcp_debug]()
        {
            const auto server_instance_id = ++gMcpServerInstanceCounter;
            if (mcp_debug)
            {
                logDiag("MCP-CONNECT",
                    "Creating MCP server instance #" + std::to_string(server_instance_id) +
                    " (new initialize attempt/session)");
            }

            try
            {
                const mcp::ErrorReporter sdk_error_reporter = [mcp_debug](const mcp::ErrorEvent& event)
                    {
                        if (!mcp_debug) return;
                        logDiag("MCP-SDK-ERROR",
                            std::string(event.component()) + ": " + std::string(event.message()));
                    };

                mcp::lifecycle::session::ToolsCapability tools_capability;
                tools_capability.listChanged = true;

                mcp::lifecycle::session::ResourcesCapability resources_capability;
                resources_capability.listChanged = true;

                mcp::server::ServerConfiguration server_config;
                server_config.sessionOptions.errorReporter = sdk_error_reporter;
                server_config.capabilities = mcp::lifecycle::session::ServerCapabilities(
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    resources_capability,
                    tools_capability,
                    std::nullopt,
                    std::nullopt);
                server_config.serverInfo = mcp::lifecycle::session::Implementation("fastmcp_server", "0.3.0");
                server_config.instructions = "Use tools for command execution and resources for plugin/app metadata. For the math form UI, call open-math-form and open http://localhost:6543/ui/math-form.html in a browser.";

                auto mcp_server = mcp::server::Server::create(std::move(server_config));

                std::set<std::string> used_tool_names;
                std::map<std::string, int> tool_name_suffixes;
                std::map<std::string, std::string> command_tool_names;

                for (const auto& meta : registry.listMetadata())
                {
                    std::string subtype_list;
                    for (const auto& st : meta.sub_cmd_types)
                    {
                        if (!subtype_list.empty()) subtype_list += ", ";
                        subtype_list += st.sub_type_name;
                    }

                    std::string full_desc = meta.description;
                    if (!subtype_list.empty())
                        full_desc += " [subType: " + subtype_list + "]";

                    json input_schema = {
                        {"type", "object"},
                        {"properties", json::object()},
                        {"additionalProperties", true}
                    };
                    std::set<std::string> required_fields;

                    for (const auto& param : meta.parameters)
                    {
                        std::string json_type = "string";
                        if (param.parameter_type == "number") json_type = "number";
                        else if (param.parameter_type == "boolean") json_type = "boolean";
                        else if (param.parameter_type == "object") json_type = "object";
                        else if (param.parameter_type == "array") json_type = "array";

                        input_schema["properties"][param.parameter_name] = {
                            {"type", json_type},
                            {"description", param.description}
                        };
                        if (param.required)
                            required_fields.insert(param.parameter_name);
                    }

                    if (!meta.sub_cmd_types.empty())
                    {
                        json enum_values = json::array();
                        for (const auto& st : meta.sub_cmd_types)
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
                        {
                            required.push_back(field);
                        }
                        input_schema["required"] = required;
                    }

                    const std::string original_cmd_name = meta.cmd_name;
                    std::string tool_name = sanitizeToolName(original_cmd_name);
                    if (used_tool_names.count(tool_name) > 0)
                    {
                        int& suffix = tool_name_suffixes[tool_name];
                        do
                        {
                            ++suffix;
                        } while (used_tool_names.count(tool_name + "_" + std::to_string(suffix)) > 0);
                        tool_name = tool_name + "_" + std::to_string(suffix);
                    }
                    used_tool_names.insert(tool_name);
                    command_tool_names[original_cmd_name] = tool_name;

                    mcp::server::ToolDefinition tool;
                    tool.name = tool_name;
                    std::string tool_description = full_desc;
                    if (tool_name != original_cmd_name)
                    {
                        tool_description += " [original: " + original_cmd_name + "]";
                    }
                    tool.description = std::move(tool_description);
                    tool.inputSchema = toMcpJson(input_schema);

                    if (mcp_debug)
                    {
                        logDiag("MCP-REGISTER",
                            "Registering tool " + tool_name + " (command=" + original_cmd_name + ")");
                    }

                    mcp_server->registerTool(
                        std::move(tool),
                        [&registry, cmd_name = original_cmd_name, tool_name, mcp_debug](const mcp::server::ToolCallContext& context) -> mcp::server::CallToolResult
                        {
                            try
                            {
                                if (mcp_debug)
                                {
                                    logRequestContext("MCP-TOOL-CALL",
                                        context.requestContext,
                                        "tool=" + tool_name + " command=" + cmd_name);
                                }

                                const json args = fromMcpJson(context.arguments);

                                auto command = registry.create(cmd_name);
                                if (!command)
                                    throw std::runtime_error("Tool not found: " + cmd_name);

                                std::string error;
                                if (!command->validate(args, error))
                                    throw std::invalid_argument("Validation failed: " + error);
                                if (!command->execute(args, error))
                                    throw std::runtime_error("Execution failed: " + error);

                                const json raw = command->getResult();
                                json structured = raw.is_object() ? raw : json{ {"result", raw} };
                                if (args.contains("subType") && args["subType"].is_string())
                                    structured["subTypeExecuted"] = args["subType"].get<std::string>();

                                std::cout << "[tools/call] " << cmd_name << " -> " << raw.dump() << '\n';

                                mcp::server::CallToolResult result;
                                result.structuredContent = toMcpJson(structured);
                                result.content = McpJson::array();
                                result.content.push_back(makeTextContent(raw.dump()));
                                result.isError = false;
                                return result;
                            }
                            catch (const std::exception& e)
                            {
                                if (mcp_debug)
                                {
                                    logDiag("MCP-TOOL-ERROR", cmd_name + " failed: " + e.what());
                                }
                                throw;
                            }
                        });
                }

                std::vector<std::string> math_subtypes;
                std::map<std::string, std::string> math_labels;
                for (const auto& meta : registry.listMetadata())
                {
                    const auto plugin_name = resolvePluginName(meta);
                    const bool is_math_command =
                        (meta.cmd_name == "math.calculate") ||
                        (toUpperAscii(plugin_name) == "MATH");
                    if (!is_math_command || meta.sub_cmd_types.empty()) continue;

                    for (const auto& st : meta.sub_cmd_types)
                    {
                        math_subtypes.push_back(st.sub_type_name);
                        math_labels[st.sub_type_name] = st.description;
                    }
                    break;
                }

                std::string math_tool_name = "math_calculate";
                if (const auto it = command_tool_names.find("math.calculate"); it != command_tool_names.end())
                {
                    math_tool_name = it->second;
                }

                mcp::server::ToolDefinition math_form_tool;
                math_form_tool.name = "open-math-form";
                math_form_tool.description = "Open a math form UI and configure operation subtypes for the math MCP tool.";

                json math_form_schema = {
                    {"type", "object"},
                    {"properties", json::object()},
                    {"required", json::array()}
                };
                math_form_tool.inputSchema = toMcpJson(math_form_schema);
                math_form_tool.annotations = mcp::jsonrpc::JsonValue::object({
                    {"readOnlyHint", true}
                    });
                math_form_tool.metadata = mcp::jsonrpc::JsonValue::object({
                    {
                        "ui", mcp::jsonrpc::JsonValue::object({
                        {"resourceUri", "http://localhost:6543/ui/math-form.html"}
                        })
                    },
                    {
                        "ui/resourceUri", "http://localhost:6543/ui/math-form.html"
                    }
                    });
                if (mcp_debug)
                {
                    logDiag("MCP-REGISTER", "Registering tool open-math-form");
                }
                mcp_server->registerTool(
                    std::move(math_form_tool),
                    [mcp_debug, math_subtypes, math_labels, math_tool_name](const mcp::server::ToolCallContext& ctx) -> mcp::server::CallToolResult
                    {
                        if (mcp_debug)
                        {
                            logRequestContext("MCP-TOOL-CALL", ctx.requestContext, "tool=open-math-form");
                        }
                        mcp::server::CallToolResult result;

                        const json response = {
                            {"status", "success"},
                            {"availability", "math-form available"},
                            {"message", "Math form UI available at http://localhost:6543/ui/math-form.html"},
                            {"resourceUri", "app://math-form"},
                            {"uiResourceUri", "http://localhost:6543/ui/math-form.html"},
                            {"toolName", math_tool_name},
                            {"subTypes", math_subtypes},
                            {"labels", math_labels}
                        };

                        result.structuredContent = toMcpJson(response);
                        result.content = McpJson::array();
                        result.content.push_back(makeTextContent(
                            "math-form available\n"
                            "resource: app://math-form\n"
                            "url: http://localhost:6543/ui/math-form.html\n"
                            "tool: " + math_tool_name));
                        result.content.push_back(makeResourceLinkContent(
                            "http://localhost:6543/ui/math-form.html",
                            "math-form-ui",
                            "Math Form UI",
                            "text/html"));
                        result.content.push_back(makeResourceLinkContent(
                            "app://math-form",
                            "math-form-resource",
                            "Math Form MCP Resource",
                            "text/html"));
                        result.content.push_back(makeTextContent(response.dump()));
                        result.isError = false;
                        return result;
                    });

                mcp::server::ResourceTemplateDefinition app_template;
                app_template.uriTemplate = "app://{path}";
                app_template.name = "app-resource-template";
                app_template.description = "Template URI for resources exposed by local mcp-apps service";
                app_template.mimeType = "text/plain";
                if (mcp_debug)
                {
                    logDiag("MCP-REGISTER", "Registering resource template app://{path}");
                }
                mcp_server->registerResourceTemplate(std::move(app_template));

                for (const auto& resource : resources)
                {
                    mcp::server::ResourceDefinition definition;
                    definition.uri = resource.uri;
                    definition.name = resource.name;
                    definition.description = resource.description;
                    definition.mimeType = resource.mime_type;

                    if (mcp_debug)
                    {
                        logDiag("MCP-REGISTER", "Registering resource " + resource.uri);
                    }

                    mcp_server->registerResource(
                        std::move(definition),
                        [resource, mcp_debug](const mcp::server::ResourceReadContext& ctx) -> std::vector<mcp::server::ResourceContent>
                        {
                            if (mcp_debug)
                            {
                                logRequestContext("MCP-RESOURCE-READ", ctx.requestContext, "uri=" + resource.uri);
                            }
                            std::string content = resource.body_provider();

                            auto item = mcp::server::ResourceContent::text(
                                resource.uri,
                                content,
                                resource.mime_type
                            );
                            return { item };
                        });
                }

                return mcp_server;
            }
            catch (...)
            {
                logDiag("MCP-CONNECT",
                    "Failed to construct MCP server instance #" + std::to_string(server_instance_id) +
                    ": " + currentExceptionMessage());
                throw;
            }
        };

    // =========================================================================
    // Auxiliary httplib REST server (non-MCP routes)
    //
    // cpp-mcp owns port N (MCP).  We run httplib on port N+1 for REST/Swagger.
    // If you want everything on one port you need a cpp-mcp version that
    // exposes get_server() — check your checkout for that accessor.
    // =========================================================================
    const int rest_port = config.port + 1;
    httplib::Server rest_server;

    // GET /
    rest_server.Get("/", [&](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_header("Cache-Control", "no-store");
            res.set_content(defaultHtmlPage(config.port, rest_port), "text/html");
        });

    // GET /swagger
    rest_server.Get("/swagger", [&](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_content(std::string(cmdsdk::swagger_resources::swagger_html), "text/html");
        });

    // GET /openapi.json
    rest_server.Get("/openapi.json", [&](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_content(
                api_aggregator.buildCombinedSpec("FastMCP API", "0.1.0").dump(2),
                "application/json");
        });

    // GET /openapi.yaml
    rest_server.Get("/openapi.yaml", [&](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_content(
                api_aggregator.buildCombinedSpec("FastMCP API", "0.1.0").dump(2),
                "application/yaml");
        });

    // GET /openapi/:plugin
    rest_server.Get("/openapi/:plugin", [&](const httplib::Request& req,
        httplib::Response& res)
        {
            addCorsHeaders(res);
            std::string pname = req.path_params.at("plugin");
            if (endsWith(pname, ".json"))       pname = pname.substr(0, pname.size() - 5);
            else if (endsWith(pname, ".yaml") || endsWith(pname, ".yml"))
                pname = pname.substr(0, pname.rfind('.'));

            const auto spec = api_aggregator.getPluginSpec(pname);
            if (spec.is_object() && !spec.empty())
            {
                res.set_content(spec.dump(2), "application/json");
            }
            else
            {
                res.status = 404;
                res.set_content(
                    json({ {"error", "Plugin spec not found: " + pname} }).dump(),
                    "application/json");
            }
        });

    // POST /api/:command — REST command execution
    if (config.protocol_mode != ProtocolMode::MCP_ONLY)
    {
        rest_server.Post("/api/:command", [&](const httplib::Request& req,
            httplib::Response& res)
            {
                addCorsHeaders(res);
                const auto cmd_name = req.path_params.at("command");
                std::cout << "[REST POST] /api/" << cmd_name << " body=" << req.body << '\n';
                json body;
                try { body = json::parse(req.body); }
                catch (const std::exception& e)
                {
                    res.status = 400;
                    res.set_content(
                        cmdsdk::RestApiHandler::buildResponse(
                            false, "Invalid JSON: " + std::string(e.what())).dump(),
                        "application/json");
                    return;
                }
                if (!body.is_object())
                {
                    res.status = 400;
                    res.set_content(
                        cmdsdk::RestApiHandler::buildResponse(false, "Body must be a JSON object").dump(),
                        "application/json");
                    return;
                }
                std::string err;
                const auto result = cmdsdk::RestApiHandler::executeCommand(
                    cmd_name, body, registry, err);
                if (!result["success"].get<bool>()) res.status = 400;
                res.set_content(result.dump(), "application/json");
            });
    }

    // GET /mcp-apps  &  /mcp-apps/:path — reverse-proxy
    auto proxy_mcp_app = [](const std::string& path, httplib::Response& res)
        {
            httplib::Client cl(kMcpAppsHost, kMcpAppsPort);
            cl.set_connection_timeout(2, 0);
            cl.set_read_timeout(5, 0);
            const auto r = cl.Get(path.c_str());
            if (!r)
            {
                res.status = 502;
                res.set_content(
                    json({ {"error", "mcp-apps unreachable"} }).dump(), "application/json");
                return;
            }
            res.status = r->status;
            res.set_content(r->body, contentTypeFromResponse(r, "text/plain"));
        };

    rest_server.Get("/mcp-apps", [&](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            std::cout << "[REST GET] /mcp-apps (proxy -> :6543/)\n";
            proxy_mcp_app("/", res);
        });
    rest_server.Get("/mcp-apps/:path", [&](const httplib::Request& req,
        httplib::Response& res)
        {
            addCorsHeaders(res);
            const auto app_path = "/" + req.path_params.at("path");
            std::cout << "[REST GET] /mcp-apps" << app_path << " (proxy -> :6543" << app_path << ")\n";
            proxy_mcp_app(app_path, res);
        });

    // CORS preflight
    rest_server.Options(".*", [](const httplib::Request&, httplib::Response& res)
        {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.status = 200;
        });

    // =========================================================================
    // Banner
    // =========================================================================
    std::cout << "\n========================================\n"
        << "FastMCP Server  (cpp-mcp-sdk by itcv-GmbH)\n"
        << "========================================\n"
        << "Protocol mode : " << protocolModeToString(config.protocol_mode) << "\n\n"
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

    // =========================================================================
    // Start servers
    // =========================================================================

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::unique_ptr<mcp::server::StreamableHttpServerRunner> mcp_runner;
    if (config.protocol_mode != ProtocolMode::REST_ONLY)
    {
        const mcp::server::ServerFactory server_factory = [createConfiguredMcpServer]()
            {
                return createConfiguredMcpServer();
            };

        mcp::server::StreamableHttpServerRunnerOptions mcp_options;
        mcp_options.transportOptions.http.errorReporter = [mcp_debug](const mcp::ErrorEvent& event)
            {
                if (!mcp_debug) return;
                logDiag("MCP-HTTP-ERROR",
                    std::string(event.component()) + ": " + std::string(event.message()));
            };
        mcp_options.transportOptions.http.endpoint.bindAddress = "0.0.0.0";
        mcp_options.transportOptions.http.endpoint.bindLocalhostOnly = false;
        mcp_options.transportOptions.http.endpoint.port = static_cast<std::uint16_t>(config.port);
        mcp_options.transportOptions.http.endpoint.path = "/mcp";
        mcp_options.transportOptions.http.requireSessionId = require_session_id;

        if (mcp_debug)
        {
            if (require_session_id)
            {
                logDiag("MCP-CONNECT",
                    "Streamable HTTP policy requireSessionId=true (strict multi-session mode). "
                    "Clients must NOT send MCP-Session-Id on initialize; "
                    "server mints MCP-Session-Id on successful initialize response.");
            }
            else
            {
                logDiag("MCP-CONNECT",
                    "Streamable HTTP policy requireSessionId=false (compat mode). "
                    "All HTTP clients share one MCP session; this improves compatibility "
                    "with proxies that do not replay MCP-Session-Id consistently.");
            }
        }

        mcp_runner = std::make_unique<mcp::server::StreamableHttpServerRunner>(server_factory, mcp_options);
        mcp_runner->start();
        std::cout << "MCP  server listening on http://0.0.0.0:" << config.port << "\n";
    }

    // REST on a background thread (detached — lives for the process lifetime)
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
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (mcp_runner)
    {
        mcp_runner->stop();
    }
    rest_server.stop();

    return 0;
}