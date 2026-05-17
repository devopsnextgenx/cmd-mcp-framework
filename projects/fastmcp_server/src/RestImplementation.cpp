// RestImplementation.cpp
#include "RestImplementation.hpp"

#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include <charconv>

#include "FastMcpStringUtils.hpp"
#include "cmdsdk/RestApiHandler.hpp"
#include "cmdsdk/SwaggerResources.hpp"
#include "HtmlTemplateString.hpp"

namespace fastmcp
{
    using json = nlohmann::json;
    using StringUtils = FastMcpStringUtils;

    // ── mcp-apps reverse-proxy helpers ────────────────────────────────────────
    static constexpr const char* kMcpAppsHost = "localhost";
    static constexpr int         kMcpAppsPort = 6543;

    static std::string contentTypeFromResponse(const httplib::Result& r, const std::string& fb)
    {
        if (!r || !r->has_header("Content-Type")) return fb;
        std::string v = r->get_header_value("Content-Type");
        const auto sep = v.find(';');
        return (sep != std::string::npos) ? v.substr(0, sep) : (v.empty() ? fb : v);
    }

    static void proxyMcpApp(const std::string& path, httplib::Response& res)
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
    }

    // ── CORS ───────────────────────────────────────────────────────────────────
    void addCorsHeaders(httplib::Response& res)
    {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                    "Content-Type, MCP-Session-Id, MCP-Protocol-Version");
    }

    // ── Homepage ───────────────────────────────────────────────────────────────
    std::string defaultHtmlPage(int mcp_port, int rest_port)
    {
        std::string result;
        result.reserve(
            html_templates::default_html_page_end.size() +
            html_templates::default_html_page_prefix.size() +
            html_templates::default_html_page_middle.size() +
            html_templates::default_html_page_suffix.size() +
            html_templates::default_html_page_end.size() +
            html_templates::default_html_page_tail.size() +
            32);

        auto appendPort = [&result](int port)
        {
            char buffer[16];
            const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), port);
            if (ec == std::errc())
                result.append(buffer, ptr - buffer);
        };

        result.append(html_templates::default_html_page_prefix);
        appendPort(mcp_port);
        result.append(html_templates::default_html_page_middle);
        appendPort(rest_port);
        result.append(html_templates::default_html_page_suffix);
        appendPort(mcp_port);
        result.append(html_templates::default_html_page_end);
        appendPort(rest_port);
        result.append(html_templates::default_html_page_tail);

        return result;
    }

    // ── Route registration ─────────────────────────────────────────────────────
    void registerRestRoutes(httplib::Server& server,
                            cmdsdk::CommandRegistry& registry,
                            cmdsdk::OpenApiAggregator& api_aggregator,
                            int mcp_port,
                            int rest_port,
                            bool protocol_mode_is_mcp_only)
    {
        // GET /
        server.Get("/", [mcp_port, rest_port](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_header("Cache-Control", "no-store");
            res.set_content(defaultHtmlPage(mcp_port, rest_port), "text/html");
        });

        // GET /swagger
        server.Get("/swagger", [](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_content(std::string(cmdsdk::swagger_resources::swagger_html), "text/html");
        });

        // GET /openapi.json
        server.Get("/openapi.json", [&api_aggregator](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_content(
                api_aggregator.buildCombinedSpec("FastMCP API", "0.1.0").dump(2),
                "application/json");
        });

        // GET /openapi.yaml
        server.Get("/openapi.yaml", [&api_aggregator](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            res.set_content(
                api_aggregator.buildCombinedSpec("FastMCP API", "0.1.0").dump(2),
                "application/yaml");
        });

        // GET /openapi/:plugin
        server.Get("/openapi/:plugin", [&api_aggregator](const httplib::Request& req,
                                                        httplib::Response& res)
        {
            addCorsHeaders(res);
            std::string pname = req.path_params.at("plugin");
            if (StringUtils::endsWith(pname, ".json"))
                pname = pname.substr(0, pname.size() - 5);
            else if (StringUtils::endsWith(pname, ".yaml") || StringUtils::endsWith(pname, ".yml"))
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

        // POST /api/:command — REST command execution (skipped in MCP-only mode)
        if (!protocol_mode_is_mcp_only)
        {
            server.Post("/api/:command", [&registry](const httplib::Request& req,
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
                        cmdsdk::RestApiHandler::buildResponse(
                            false, "Body must be a JSON object").dump(),
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

        // GET /mcp-apps  (root proxy)
        server.Get("/mcp-apps", [](const httplib::Request&, httplib::Response& res)
        {
            addCorsHeaders(res);
            std::cout << "[REST GET] /mcp-apps (proxy -> :6543/)\n";
            proxyMcpApp("/", res);
        });

        // GET /mcp-apps/:path  (sub-path proxy)
        server.Get(R"(/mcp-apps/(.+))", [](const httplib::Request& req, httplib::Response& res)
        {
            addCorsHeaders(res);
            const auto app_path = "/" + req.matches[1].str();
            std::cout << "[REST GET] /mcp-apps" << app_path
                    << " (proxy -> :6543" << app_path << ")\n";
            proxyMcpApp(app_path, res);
        });

        // CORS preflight
        server.Options(".*", [](const httplib::Request&, httplib::Response& res)
        {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type");
            res.status = 200;
        });
    }
} // namespace fastmcp