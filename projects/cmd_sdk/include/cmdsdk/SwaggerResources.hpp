#pragma once

#include <string_view>

namespace cmdsdk {

/**
 * SwaggerResources
 *
 * Embedded Swagger UI resource served by /swagger.
 * Uses official swagger-ui-dist assets from CDN and loads /openapi.json.
 */
namespace swagger_resources {

constexpr std::string_view swagger_html = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>FastMCP Swagger UI</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist/swagger-ui.css">
</head>
<body>
  <div id="swagger-ui"></div>

  <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js" crossorigin></script>
  <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-standalone-preset.js" crossorigin></script>
  <script>
    window.onload = function () {
      window.ui = SwaggerUIBundle({
        url: '/openapi.json',
        dom_id: '#swagger-ui',
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        layout: 'StandaloneLayout',
        deepLinking: true
      });
    };
  </script>
</body>
</html>)";

}  // namespace swagger_resources
}  // namespace cmdsdk
