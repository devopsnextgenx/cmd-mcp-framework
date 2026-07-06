## graphify

For any question about this repo's architecture, structure, components, or how to add/modify/find
code, your **first tool call must be** to read `graphify-out/GRAPH_REPORT.md` (if it exists).

Triggers: "how do I…", "where is…", "what does … do", "add/modify a <component>",
"explain the architecture", or anything that depends on how files or classes relate.

After reading the report (and `graphify-out/wiki/index.md` for deep questions), answer from the
graph. Only read source files when (a) modifying/debugging specific code, (b) the graph lacks
the needed detail, or (c) the graph is missing or stale.

Type `/graphify` in Copilot Chat to build or update the graph.

## Build & Compilation Instructions

When writing, refactoring, or troubleshooting code, always ensure the changes adhere to the project's build system.

- **Primary Build Command:** `bash build.sh`
- **Docker Build Command:** `docker build -t cmd-mcp-server:local .`
- **Development/Watch Command[mcp server]:** `FASTMCP_MCP_DEBUG=1 ./build/bin/fastmcp_server`
- **docker run Command[mcp server]:** `docker run --rm --name cmd-mcp-framework-test -p 5432:5432 -p 5433:5433 -p 6543:6543 -e FASTMCP_MCP_DEBUG=1 localhost/cmd-mcp-server:local`
- **Development/Watch Command[mcp-apps/dashboard-ui]:** `cd mcp-apps/dashboard-ui && npm run start`