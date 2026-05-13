# cmd-mcp-framework

## Quick testing


## Quick Test using MCP Inspector

```bash
npx @modelcontextprotocol/inspector --url http://localhost:5432/mcp --transport http
npx @modelcontextprotocol/inspector@0.15.0 -- --url http://localhost:5432/mcp --transport http
```

## Implementation

A multi-project C/C++ framework for exposing command plugins through a FastMCP-style server with MCP over HTTP (streamable HTTP transport).

This repository is structured so that:
- one project builds an executable server (`fastmcp_server`)
- one project builds a shared SDK (`cmd_sdk`) used by command developers
- plugin projects build shared libraries (`.so`/`.dll`) that register commands and business logic

The server publishes command metadata (name, description, parameters, validation rules, subtype docs) as MCP tools and allows invocation over MCP JSON-RPC over HTTP.

## What this framework provides
- CMake super-project with isolated subprojects
- `ICmd` interface and abstract `Cmd` base class
- `SubCmd` base class for command-internal subtype switching
- dynamic plugin loading (`RegisterCommands` entrypoint)
- command registration metadata for documentation/resource exposure
- HTTP server on configurable port (default 5432)
- endpoints for:
  - default info page (`/`)
  - MCP JSON-RPC (`/mcp`)

## Project layout
- `CMakeLists.txt`
  - top-level super-build and dependency fetch (`nlohmann/json`, `cpp-httplib`)
- `projects/cmd_sdk/`
  - c/c++ SDK shared library with command abstractions and registry
  - headers under `projects/cmd_sdk/include/cmdsdk/`
- `projects/plugins/math_cmd_provider/`
  - c/c++ sample plugin shared library with one `SubCmd` implementation (`math.calculate`)
- `projects/fastmcp_server/`
  - c/c++ server executable that loads plugins and exposes endpoints on port `5432`
- `mcp-apps/dashboard-ui`
  - React based `@modelcontextprotocol/ext-apps` components for exposing apps as resources on port `6543` in mcp-server running using `fastmcp_server`
  
## Core abstractions

### 1) `ICmd` interface
Defined in `projects/cmd_sdk/include/cmdsdk/ICmd.hpp`.

Every command must implement:
- `validate(const nlohmann::json& input, std::string& error)`
- `execute(const nlohmann::json& input, std::string& error)`
- `getResult() const`

### 2) Abstract `Cmd` class
Defined in `projects/cmd_sdk/include/cmdsdk/Cmd.hpp`.

`Cmd` implements result storage (`getResult`, protected `setResult`) and stays abstract because `validate` and `execute` are still pure virtual from `ICmd`.

### 3) `SubCmd` class with subtype registration
Defined in `projects/cmd_sdk/include/cmdsdk/SubCmd.hpp`.

`SubCmd` supports:
- registering subtype labels via `registerSubCmdType(...)`
- resolving subtype strings via `resolveSubCmdType(...)`
- validation of subtype format: `<PluginName>.<SubCmdType>` with max length 128
- duplicate detection and warning logging

Typical pattern:
1. Set plugin name in constructor using `setPluginName("PLUGIN_NAME")`
2. Register subtypes with format "PLUGIN_NAME.SUBTYPE" in constructor
3. Parse `"subType"` in `execute` (input should be "PLUGIN_NAME.SUBTYPE")
4. Dispatch with `if-else` chain to `executeSubTypeA/B/C`

Plugin naming constraints:
- Plugin name must be consistent across all subtypes for a plugin
- Subtype format: `<PluginName>.<PreferredSubCmdType>`
- Max length: 128 characters
- Duplicates or conflicts logged as warnings during initialization

### 4) Command metadata for docs/resources
Defined in `projects/cmd_sdk/include/cmdsdk/CommandMetadata.hpp`.

Each command registration includes:
- `cmd_name`
- `description`
- `parameters` (`name`, `type`, `required`, validation, description)
- `sub_cmd_types` (`name`, description, `response_schema`)

`response_schema` is a JSON Schema object describing the expected result payload
for that specific `subType`. The server surfaces this in MCP `tools/list`
metadata so an LLM can decide what subtype to call based on expected output
shape before invocation.

This metadata is what the server returns in MCP `tools/list` and allows invocation via `tools/call`.

### 5) Registry and plugin API
- Registry: `projects/cmd_sdk/include/cmdsdk/CommandRegistry.hpp`
- Plugin ABI entrypoint: `projects/cmd_sdk/include/cmdsdk/PluginApi.hpp`

Each plugin exports:
```cpp
extern "C" CMDSDK_API void RegisterCommands(cmdsdk::CommandRegistry& registry)
```

Inside that function, the plugin registers command metadata plus a factory function.

The server exposes these as MCP tools via the `/mcp` endpoint.

## Build

### Prerequisites
- CMake `>= 3.20`
- C++20-capable compiler (GCC/Clang/MSVC)
- network access during initial configure (for FetchContent dependencies)

### Configure and compile
From repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

### Expected build outputs
- server executable:
  - `build/bin/fastmcp_server`
- SDK shared library:
  - Linux/macOS: `build/lib/libcmd_sdk.so` / `build/lib/libcmd_sdk.dylib`
  - Windows runtime DLL: `build/bin/cmd_sdk.dll` (import library in `build/lib`)
- sample plugin:
  - Linux/macOS: `build/lib/libmath_cmd_provider.so` / `build/lib/libmath_cmd_provider.dylib`
  - Windows runtime DLL: `build/bin/math_cmd_provider.dll` (import library in `build/lib`)

## Run

### Start server with default plugin resolution
```bash
./build/bin/fastmcp_server
```

By default, the server loads all plugins found in the `build/lib/` directory.

### Start server with custom port and explicit plugin list
You can specify port and plugins:

```bash
./build/bin/fastmcp_server --port=8080 --plugins=./build/lib/libmath_cmd_provider.so,./build/lib/libother_plugin.so
```

### Server port
The server listens on port `5432` by default, configurable via `--port`.

## Runtime endpoints

### `GET /`
Default HTML page describing available endpoints and MCP usage hints.

### `POST /mcp`
MCP JSON-RPC endpoint for tools and resources.

## MCP Examples

### List tools
```bash
curl -s -X POST http://127.0.0.1:5432/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":1,
    "method":"tools/list",
    "params":{}
  }'
```

### Call math.calculate tool
```bash
curl -s -X POST http://127.0.0.1:5432/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc":"2.0",
    "id":2,
    "method":"tools/call",
    "params":{
      "name":"math.calculate",
      "arguments":{
        "subType":"MATH.MUL",
        "left":6,
        "right":7
      }
    }
  }'
```

Expected result includes:
- `content[0].text` with JSON result from the command
- `result.value: 42.0`

## Extending the project

### A) Add a new plugin project
1. Create a new folder under `projects/plugins/`, e.g. `projects/plugins/my_business_plugin/`
2. Add a `CMakeLists.txt` that builds a `SHARED` library
3. Link against `cmd_sdk` (and any extra libs you need)
4. Add the plugin subdirectory in root `CMakeLists.txt`:
   - `add_subdirectory(projects/plugins/my_business_plugin)`

Minimal plugin CMake:

```cmake
add_library(my_business_plugin SHARED
  src/MyCommand.cpp
)

target_link_libraries(my_business_plugin
  PRIVATE
    cmd_sdk
    nlohmann_json::nlohmann_json
)
```

### B) Implement a command (`Cmd`-based)
Use this when you do not need internal subtype dispatch.

1. Derive from `cmdsdk::Cmd`
2. Implement:
   - `validate(...)`
   - `execute(...)`
3. Call `setResult(...)` in execute path
4. Register metadata + factory in `RegisterCommands(...)`

### C) Implement a command (`SubCmd`-based with subtype switch)
Use this when one command exposes multiple internal business actions (`SubCmdType`).

1. Derive from `cmdsdk::SubCmd`
2. In constructor, set plugin name with `setPluginName("YOUR_PLUGIN_NAME")`
3. Register subtypes with `registerSubCmdType("YOUR_PLUGIN_NAME.SUBTYPE", ...)` for each subtype
4. In `execute(...)`, resolve `subType` and dispatch with `if-else` chain
5. Keep subtype-specific business logic in helpers like:
   - `executeSubTypeA(...)`
   - `executeSubTypeB(...)`
   - `executeSubTypeC(...)`

Subtype naming constraints:
- Must follow format: `<PluginName>.<SubCmdType>`
- PluginName must be consistent for all subtypes in the plugin
- Max length: 128 characters
- Input `subType` parameter should use the full "PLUGIN_NAME.SUBTYPE" format

This pattern is demonstrated by:
- `projects/plugins/math_cmd_provider/src/MathCmdProvider.cpp`

### D) Register command metadata for MCP resources
Inside plugin `RegisterCommands(...)`, provide `CommandMetadata` with:
- command name (`cmd_name`)
- human-readable description
- parameter definitions + validation semantics
- subtype documentation (if applicable)

Example registration pattern for SubCmd:

```cpp
class MySubCmdProvider : public cmdsdk::SubCmd {
 public:
  MySubCmdProvider() {
    setPluginName("MYPLUGIN");
    registerSubCmdType("MYPLUGIN.TYPE_A", {"MYPLUGIN.TYPE_A", "Subtype A behavior"});
    registerSubCmdType("MYPLUGIN.TYPE_B", {"MYPLUGIN.TYPE_B", "Subtype B behavior"});
  }

  bool execute(const nlohmann::json& input, std::string& error) override {
    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());
    if (sub_type == "MYPLUGIN.TYPE_A") {
      // execute type A logic
    } else if (sub_type == "MYPLUGIN.TYPE_B") {
      // execute type B logic
    }
    return true;
  }
};

extern "C" CMDSDK_API void RegisterCommands(cmdsdk::CommandRegistry& registry) {
  cmdsdk::CommandMetadata md;
  md.cmd_name = "my.command";
  md.description = "Does business operation X";
  md.parameters = {
    {"subType", "string", true, "Allowed values: MYPLUGIN.TYPE_A, MYPLUGIN.TYPE_B", "Selects operation type"},
    {"param", "string", true, "non-empty", "Operation parameter"}
  };
  md.sub_cmd_types = {
    {"MYPLUGIN.TYPE_A", "Subtype A behavior", {
      {"type", "object"},
      {"required", {"subTypeExecuted", "value"}},
      {"properties", {
        {"subTypeExecuted", {{"type", "string"}, {"const", "MYPLUGIN.TYPE_A"}}},
        {"value", {{"type", "number"}}}
      }}
    }},
    {"MYPLUGIN.TYPE_B", "Subtype B behavior", {
      {"type", "object"},
      {"required", {"subTypeExecuted", "status"}},
      {"properties", {
        {"subTypeExecuted", {{"type", "string"}, {"const", "MYPLUGIN.TYPE_B"}}},
        {"status", {{"type", "string"}}}
      }}
    }}
  };

  std::string error;
  registry.registerCommand(md, []() -> std::unique_ptr<cmdsdk::ICmd> {
    return std::make_unique<MySubCmdProvider>();
  }, error);
}
```

### E) Run server with your plugin
Pass your plugin path explicitly while developing:

```bash
./build/bin/fastmcp_server --plugins=./build/lib/libmy_business_plugin.so
```

After loading succeeds, verify:
1. `tools/list` contains your command metadata
2. `tools/call` works with sample payloads

## Development tips
- Keep `validate` strict and return actionable error messages
- Make `execute` side effects explicit and deterministic
- Use metadata descriptions as user-facing docs (these are published directly)
- Prefer one command per business capability; use subtypes when operations are tightly related

## Troubleshooting
- `Plugin not found: ...`
  - verify plugin output path and filename (prefix/suffix are platform-specific)
- `RegisterCommands symbol not found ...`
  - ensure exact exported function signature and `extern "C"`
- `Command not found: ...`
  - confirm registration succeeded and `cmd_name` matches invocation
- `Validation failed: ...`
  - inspect `params.args` shape against metadata parameter and subtype rules
- `Failed to bind server on port 5432`
  - port is in use; stop conflicting process or change server port compile definition

## Current sample command
The sample plugin currently publishes:
- `math.calculate`
  - `subType`: `MATH.ADD | MATH.SUB | MATH.MUL | MATH.DIV | MATH.MOD | MATH.POW`
  - `left`, `right`: numeric operands
  - subtype behaviors:
    - `MATH.ADD` -> addition
    - `MATH.SUB` -> subtraction
    - `MATH.MUL` -> multiplication
    - `MATH.DIV` -> division
    - `MATH.MOD` -> modulo
    - `MATH.POW` -> power

Use this as a template for your own domain-specific commands. Plugin developers should define a consistent plugin name (e.g., "MATH") and use the format `<PluginName>.<SubCmdType>` for all subtypes.
