# Plugin and Provider Writing Skill

Use this skill when adding a new plugin to the framework or when adding a new provider to an existing plugin.

## When to use this skill
- You are creating a new MCP tool provider under [projects/plugins](projects/plugins).
- You are extending an existing plugin with another provider class.
- The new tool should be exposed as a normal tool or as an MCP app tool with a UI.
- You need to update CMake wiring for the plugin or its provider sources.

## Repository conventions
- Plugin sources live under [projects/plugins](projects/plugins).
- Each plugin usually has its own folder, a CMakeLists.txt, and one or more provider implementations under src/.
- Provider implementations should derive from cmdsdk::SubCmd and register themselves with CMDSDK_REGISTER_PROVIDER(...).
- MCP app tool UIs live under [mcp-apps/dashboard-ui](mcp-apps/dashboard-ui).

## Add a new plugin

1. Create a new plugin directory under [projects/plugins](projects/plugins), for example [projects/plugins/greeting_cmd_provider](projects/plugins/greeting_cmd_provider).
2. Add a CMakeLists.txt for the plugin.
3. Add one or more provider source files under src/.
4. Register the plugin in the root [CMakeLists.txt](CMakeLists.txt) with add_subdirectory(projects/plugins/<plugin_name>).

### Plugin CMake pattern
Use this pattern for the plugin CMake file:

```cmake
add_library(<plugin_name> SHARED
  src/<ProviderName>.cpp
  ${CMDSDK_REGISTRAR_SRC}
)

target_link_libraries(<plugin_name>
  PRIVATE
    cmd_sdk
    nlohmann_json::nlohmann_json
)

set_target_properties(<plugin_name> PROPERTIES OUTPUT_NAME "<plugin_name>")
```

### Important CMake details
- Always link against cmd_sdk and nlohmann_json::nlohmann_json.
- Include ${CMDSDK_REGISTRAR_SRC} so the provider registration macro resolves.
- Keep the output name aligned with the plugin library name that should be discovered at runtime.

## Add a new provider under an existing plugin

1. Add a new .cpp file under the plugin's src/ directory.
2. Update that plugin's CMakeLists.txt to include the new source file in the add_library(...) call.
3. Implement the provider class in the new file.
4. Rebuild the project so the new provider is compiled into the shared library.

### Provider implementation pattern
A provider should typically:

```cpp
class MyProvider final : public cmdsdk::SubCmd {
 public:
  MyProvider() : cmdsdk::SubCmd() {
    setPluginName("MYPLUGIN");
  }

  cmdsdk::CommandMetadata buildMetadata() const override {
    cmdsdk::CommandMetadata metadata;
    metadata.cmd_name = "myplugin.do_action";
    metadata.description = "Describe what the tool does.";
    metadata.parameters = {
      {"name", "string", false, "Optional name for the operation.", "Name input."},
    };
    metadata.is_tool = true;
    return metadata;
  }

  bool validate(const nlohmann::json& input, std::string& error) override {
    if (!input.is_object()) {
      error = "Input must be a JSON object.";
      return false;
    }
    return true;
  }

  bool execute(const nlohmann::json& input, std::string& error) override {
    setResult({{"message", "Done"}});
    return true;
  }
};

CMDSDK_REGISTER_PROVIDER(MyProvider);
```

## Expose the tool as an MCP app tool with UI

If the tool should open a form or richer UI experience, mark it as an app tool.

### Metadata requirements
In buildMetadata():

```cpp
metadata.is_app_tool = true;
metadata.resource_uri = "ui://ui/<resource-name>";
```

This enables the server to register the tool with a UI resource.

### UI resource wiring
For a UI-backed tool, add the resource in [mcp-apps/dashboard-ui/vite.config.ts](mcp-apps/dashboard-ui/vite.config.ts) and, if needed, in [mcp-apps/dashboard-ui/server.ts](mcp-apps/dashboard-ui/server.ts).

The UI resource should include:
- a unique resource URI such as ui://ui/greet.html
- a human-friendly name and description
- a matching _meta.ui.resourceUri entry

### UI file layout
Typical files for an MCP app UI are:
- [mcp-apps/dashboard-ui/ui](mcp-apps/dashboard-ui/ui) /<resource-name>.html
- [mcp-apps/dashboard-ui/src](mcp-apps/dashboard-ui/src)/<resource-name>/<resource-name>.tsx

The HTML entry should mount the React component, and the React component should implement the form or view for the tool.

## Optional OpenAPI support
If the plugin should expose HTTP/OpenAPI metadata, add an openapi.spec.yml file beside the plugin sources. The loader in the cmd_sdk layer will discover and load it automatically.

## Example from this repository
The Hello World provider is a good reference:
- provider implementation: [projects/plugins/greeting_cmd_provider/src/GreetingCmdProvider.cpp](projects/plugins/greeting_cmd_provider/src/GreetingCmdProvider.cpp)
- plugin CMake: [projects/plugins/greeting_cmd_provider/CMakeLists.txt](projects/plugins/greeting_cmd_provider/CMakeLists.txt)
- app UI resource: [mcp-apps/dashboard-ui/vite.config.ts](mcp-apps/dashboard-ui/vite.config.ts)

## Verify the change
Run these checks after adding or editing a provider:

```bash
cmake -S . -B build
cmake --build build -j4
```

If you changed the dashboard UI, also verify the UI build:

```bash
cd mcp-apps/dashboard-ui
npm install
npm run build-ui
```

## Checklist
- [ ] New plugin folder exists under [projects/plugins](projects/plugins)
- [ ] Plugin CMakeLists.txt exists and links the correct targets
- [ ] Provider source file exists and registers with CMDSDK_REGISTER_PROVIDER
- [ ] Root [CMakeLists.txt](CMakeLists.txt) includes the plugin
- [ ] Tool metadata is correct for normal tools or app tools
- [ ] UI resource is registered if the tool is an MCP app tool
- [ ] Build succeeds after the change
