#pragma once

#include <string_view>

namespace cmdsdk {

/**
 * SwaggerResources
 *
 * Embedded Swagger UI resources as constexpr strings.
 * These are compiled into the binary, no filesystem access needed.
 *
 * The /swagger endpoint serves these resources, which load OpenAPI spec
 * from /openapi.json.
 */
namespace swagger_resources {

// Minified Swagger UI HTML
constexpr std::string_view swagger_html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FastMCP Swagger UI</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@3/swagger-ui.css">
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            background: #1e1e1e;
            color: #e0e0e0;
            font-family: 'Segoe UI', sans-serif;
        }
        .swagger-ui {
            background: #1e1e1e;
        }
        .swagger-ui .info .title {
            color: #4da6ff;
        }
        .swagger-ui .topbar {
            background-color: #252526;
            border-bottom: 2px solid #404040;
        }
        .swagger-ui .topbar .topbar-title {
            color: #fff;
        }
        .swagger-ui .opblock {
            background: #2d2d2d;
            border: 1px solid #404040;
            margin: 0 0 10px 0;
        }
        .swagger-ui .opblock.opblock-post {
            background: rgba(76, 140, 170, 0.1);
            border-color: #4c8caa;
        }
        .swagger-ui .opblock-summary {
            border-bottom: 1px solid #404040;
        }
        .swagger-ui .opblock-summary-post {
            border-color: #4c8caa;
        }
        .swagger-ui .parameter__name {
            color: #9cdcfe;
        }
        .swagger-ui .model-box {
            background: #2d2d2d;
            border: 1px solid #404040;
        }
        .swagger-ui section.models {
            background: #1e1e1e;
        }
        .swagger-ui .responses-wrapper {
            background: #2d2d2d;
        }
        .swagger-ui .response-col_status {
            background: #1e1e1e;
        }
        .topbar {
            padding: 10px 0;
        }
        #topbar-title {
            font-size: 1.5em;
            color: #fff;
            padding: 10px 30px;
        }
        .topbar ul {
            list-style: none;
            display: flex;
            align-items: center;
            padding: 0 30px;
        }
        .topbar li {
            margin: 0 20px 0 0;
        }
        .topbar a {
            color: #4da6ff;
            text-decoration: none;
        }
        .topbar a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div id="swagger-ui"></div>
    <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@3/swagger-ui.js"></script>
    <script>
        window.onload = function() {
            window.ui = SwaggerUIBundle({
                url: "/openapi.json",
                dom_id: '#swagger-ui',
                presets: [
                    SwaggerUIBundle.presets.apis,
                    SwaggerUIBundle.SwaggerUIStandalonePreset
                ],
                layout: "BaseLayout",
                deepLinking: true,
                defaultModelsExpandDepth: 1,
                defaultModelExpandDepth: 2,
                showOperationFilterTag: true,
                showOperationFilterButton: true,
                tryItOutEnabled: true,
                filter: true,
                tagsSorter: "alpha",
                operationsSorter: "alpha"
            })
        }
    </script>
</body>
</html>)";

// Alternative: Lightweight local Swagger UI variant (no CDN fallback)
constexpr std::string_view swagger_minimal_html = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FastMCP Swagger UI</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            background: #1e1e1e;
            color: #e0e0e0;
            font-family: 'Segoe UI', Tahoma, sans-serif;
        }
        .topbar {
            background-color: #252526;
            border-bottom: 2px solid #404040;
            padding: 15px 30px;
        }
        .topbar h1 {
            color: #fff;
            font-size: 1.5em;
            margin-bottom: 10px;
        }
        .topbar p {
            color: #a0a0a0;
            font-size: 0.9em;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        .info-box {
            background: #2d2d2d;
            border-left: 4px solid #4da6ff;
            padding: 20px;
            margin-bottom: 20px;
            border-radius: 4px;
        }
        .info-box h2 {
            color: #4da6ff;
            margin-bottom: 10px;
        }
        .endpoint {
            background: #2d2d2d;
            border: 1px solid #404040;
            margin-bottom: 15px;
            border-radius: 4px;
            overflow: hidden;
        }
        .endpoint-header {
            background: #1a1a1a;
            padding: 15px 20px;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid #404040;
        }
        .endpoint-method {
            background: #4c8caa;
            color: #fff;
            padding: 4px 12px;
            border-radius: 3px;
            font-weight: bold;
            margin-right: 15px;
            font-size: 0.9em;
        }
        .endpoint-path {
            color: #9cdcfe;
            font-family: 'Courier New', monospace;
            flex: 1;
        }
        .endpoint-toggle {
            color: #a0a0a0;
            font-size: 1.2em;
        }
        .endpoint-content {
            padding: 20px;
            display: none;
        }
        .endpoint-content.expanded {
            display: block;
        }
        .section {
            margin-bottom: 15px;
        }
        .section-title {
            color: #4da6ff;
            font-weight: bold;
            margin-bottom: 8px;
            font-size: 0.95em;
        }
        .code-block {
            background: #1a1a1a;
            border: 1px solid #404040;
            padding: 10px;
            border-radius: 3px;
            font-family: 'Courier New', monospace;
            font-size: 0.85em;
            overflow-x: auto;
            color: #9cdcfe;
        }
        .tag {
            display: inline-block;
            background: #3a4a5a;
            color: #9cdcfe;
            padding: 3px 8px;
            border-radius: 3px;
            font-size: 0.8em;
            margin-right: 8px;
        }
        .try-it-btn {
            background: #4c8caa;
            color: #fff;
            border: none;
            padding: 8px 16px;
            border-radius: 3px;
            cursor: pointer;
            font-size: 0.9em;
        }
        .try-it-btn:hover {
            background: #5a99bb;
        }
        .response-section {
            margin-top: 15px;
            padding-top: 15px;
            border-top: 1px solid #404040;
        }
    </style>
</head>
<body>
    <div class="topbar">
        <h1>FastMCP API Explorer</h1>
        <p>Interactive API documentation • Auto-generated from CommandMetadata</p>
    </div>
    <div class="container" id="endpoints-container">
        <div class="info-box">
            <h2>Loading API Documentation...</h2>
            <p>Fetching OpenAPI specification from /openapi.json</p>
        </div>
    </div>
    <script>
        async function loadAndRenderSpec() {
            try {
                const response = await fetch('/openapi.json');
                const spec = await response.json();
                const container = document.getElementById('endpoints-container');
                container.innerHTML = '';

                // Render info
                const infoBox = document.createElement('div');
                infoBox.className = 'info-box';
                infoBox.innerHTML = `
                    <h2>${spec.info.title}</h2>
                    <p>${spec.info.description || 'API Documentation'}</p>
                    <p>Version: ${spec.info.version}</p>
                `;
                container.appendChild(infoBox);

                // Render endpoints
                if (spec.paths) {
                    for (const [path, pathItem] of Object.entries(spec.paths)) {
                        for (const [method, operation] of Object.entries(pathItem)) {
                            if (method.toLowerCase() !== 'post') continue;

                            const ep = document.createElement('div');
                            ep.className = 'endpoint';

                            const tag = (operation.tags && operation.tags[0]) || 'API';
                            const summary = operation.summary || operation.operationId || path;

                            ep.innerHTML = `
                                <div class="endpoint-header">
                                    <span class="endpoint-method">POST</span>
                                    <span class="endpoint-path">${path}</span>
                                    <span class="endpoint-toggle">▼</span>
                                </div>
                                <div class="endpoint-content">
                                    <div class="section">
                                        <div class="section-title">Description</div>
                                        <p>${summary}</p>
                                    </div>
                                    <div class="section">
                                        <div class="section-title">Tag</div>
                                        <span class="tag">${tag}</span>
                                    </div>
                                    <div class="section">
                                        <div class="section-title">Request Body</div>
                                        <div class="code-block">${JSON.stringify(operation.requestBody, null, 2)}</div>
                                    </div>
                                    <div class="section">
                                        <div class="section-title">Responses</div>
                                        <div class="code-block">${JSON.stringify(operation.responses, null, 2)}</div>
                                    </div>
                                </div>
                            `;

                            const header = ep.querySelector('.endpoint-header');
                            header.addEventListener('click', () => {
                                ep.querySelector('.endpoint-content').classList.toggle('expanded');
                                ep.querySelector('.endpoint-toggle').textContent =
                                    ep.querySelector('.endpoint-content').classList.contains('expanded') ? '▲' : '▼';
                            });

                            container.appendChild(ep);
                        }
                    }
                }
            } catch (err) {
                document.getElementById('endpoints-container').innerHTML =
                    `<div class="info-box"><h2>Error</h2><p>${err.message}</p></div>`;
            }
        }
        window.onload = loadAndRenderSpec;
    </script>
</body>
</html>)";

}  // namespace swagger_resources
}  // namespace cmdsdk
