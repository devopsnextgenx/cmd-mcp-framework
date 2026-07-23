# mcp-python-server

Python host process for:
- MCP streamable HTTP server on port 5432
- REST API server on port 5433
- Swagger documentation for REST endpoints
- Dynamic command/tool execution through cmdsdk_bridge shared library and plugin shared libraries

## Prerequisites

1. Build the C++ targets from repo root:

```powershell
bash build.sh
```

2. Ensure bridge and plugin shared libraries exist.
  - Linux/macOS: `build/lib`
  - Windows: `build/bin`

## Python setup with uv

```powershell
cd mcp-python-server
uv sync
```

## Run

```powershell
uv run mcp-python-server
```

## Environment variables

- `MCP_PYTHON_BRIDGE_LIB` (or legacy `MCP_PYTHON_BRIDGE_DLL`):
  - Default (Linux): `../build/lib/libcmdsdk_bridge.so`
  - Default (macOS): `../build/lib/libcmdsdk_bridge.dylib`
  - Default (Windows): `../build/bin/cmdsdk_bridge.dll`
- `MCP_PYTHON_PLUGIN_LIBS` (or legacy `MCP_PYTHON_PLUGIN_DLLS`):
  - Semicolon/comma-separated plugin list.
  - Default (Linux): `../build/lib/libgreeting_cmd_provider.so,../build/lib/libmath_cmd_provider.so`
  - Default (macOS): `../build/lib/libgreeting_cmd_provider.dylib,../build/lib/libmath_cmd_provider.dylib`
  - Default (Windows): `../build/bin/greeting_cmd_provider.dll,../build/bin/math_cmd_provider.dll`
- `MCP_PYTHON_MCP_HOST`: default `0.0.0.0`
- `MCP_PYTHON_MCP_PORT`: default `5432`
- `MCP_PYTHON_MCP_PATH`: default `/mcp`
- `MCP_PYTHON_REST_HOST`: default `0.0.0.0`
- `MCP_PYTHON_REST_PORT`: default `5433`

## Endpoints

REST server:
- `GET /` dashboard
- `GET /docs` swagger ui
- `GET /api/commands`
- `POST /api/commands/{command_name}`
- `GET /api/sessions/{session_id}`
- `DELETE /api/sessions/{session_id}`
- `POST /api/mcp-proxy`

MCP server:
- `POST /mcp`
- `GET /mcp`
- `DELETE /mcp`

The MCP tools are registered from plugin metadata discovered through the bridge.
