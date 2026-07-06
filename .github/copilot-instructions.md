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
- **Docker run Command[mcp server]:** `docker run --rm --name cmd-mcp-framework-test -p 5432:5432 -p 5433:5433 -p 6543:6543 -e FASTMCP_MCP_DEBUG=1 localhost/cmd-mcp-server:local`
- **Development/Watch Command[mcp-apps/dashboard-ui]:** `cd mcp-apps/dashboard-ui && npm run start`

## Dependency src change

When you need to make changes to a dependency source file, follow these steps:

- Analyze the dependency's source code to understand the impact of your changes.
- Make the necessary modifications to the source file. and write it to patch folder with with maintaining folder structure hierarchy. 
  - ie. Dependency source file: build/_deps/mcp_sdk-src/src/lifecycle/session.cpp
  - Create file: patch/mcp_sdk-src/src/lifecycle/session.cpp
- Run create-patch.sh to generate a patch file for the changes made.
  - This generates file patch/mcp_sdk-src/src/lifecycle/session.cpp.patch
- During build, the patch will be applied to the dependency source file automatically. You can verify that the patch was applied correctly by checking the build logs or inspecting the modified source file in the build directory.