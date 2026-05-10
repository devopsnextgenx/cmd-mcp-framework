#pragma once

#include <string_view>

namespace cmdsdk {

/**
// Official Swagger UI HTML using swagger-ui-dist assets from CDN.
 *
 * Embedded Swagger UI resources as constexpr strings.
 * These are compiled into the binary, no filesystem access needed.
 *
    <meta name="viewport" content="width=device-width,initial-scale=1">
 * from /openapi.json.
    <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist/swagger-ui.css">
            margin: 0 20px 0 0;
        }
        .topbar a {
    <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js" crossorigin></script>
    <script src="https://unpkg.com/swagger-ui-dist/swagger-ui-standalone-preset.js" crossorigin></script>
            text-decoration: none;
        window.onload = function () {
            window.ui = SwaggerUIBundle({
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div id="swagger-ui"></div>
                layout: "StandaloneLayout",
                deepLinking: true
            });
        };
                defaultModelsExpandDepth: 1,
                defaultModelExpandDepth: 2,
                showOperationFilterTag: true,

        .swagger-ui {
