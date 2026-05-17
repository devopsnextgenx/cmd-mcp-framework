#ifndef FASTMCP_SERVER_HTML_TEMPLATES_HPP
#define FASTMCP_SERVER_HTML_TEMPLATES_HPP

#include <string_view>

namespace fastmcp::html_templates
{
    constexpr std::string_view default_html_page_prefix = R"HTML(<!DOCTYPE html>
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
        <code>POST http://localhost:)HTML";

    constexpr std::string_view default_html_page_middle = R"HTML(/mcp</code>
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
    <p class="text-secondary small mb-2">Port )HTML";

    constexpr std::string_view default_html_page_suffix = R"HTML(</p>
    <ul class="list-unstyled small mb-0">
    <li><code>GET  /swagger</code> — Interactive UI</li>
    <li><code>GET  /openapi.json</code> — OpenAPI spec</li>
    <li><code>GET  /openapi.yaml</code> — YAML OpenAPI</li>
    <li><code>POST /api/{command}</code> — REST execution</li>
    <li><code>GET  /mcp-apps/*</code> — App proxy (:6543)</li>
    </ul>
    </div>
    </div>
</div>
<p class="text-secondary small">MCP port <strong>)HTML";

    constexpr std::string_view default_html_page_end = R"HTML(</strong>
        &nbsp;·&nbsp; REST port <strong>)HTML";

    constexpr std::string_view default_html_page_tail = R"HTML(</strong>
        &nbsp;·&nbsp; cpp-mcp-sdk library (itcv-GmbH)</p>
</div>
</body>
</html>)HTML";
}

#endif // FASTMCP_SERVER_HTML_TEMPLATES_HPP
