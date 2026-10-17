#include "cmdsdk_bridge/CmdSdkBridge.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using json = nlohmann::json;

struct SessionContext {
  std::string session_id;
  std::size_t command_count{0};
  std::string last_command;
  json last_arguments = json::object();
  json last_result = json::object();
  std::vector<json> history;
};

struct LoadedPlugin {
  std::filesystem::path path;
  void* handle{nullptr};
};

class Runtime {
 public:
  Runtime() = default;
  ~Runtime() { unloadAll(); }

  bool loadPlugin(const std::filesystem::path& plugin_path, std::string& error) {
    if (!std::filesystem::exists(plugin_path)) {
      error = "Plugin not found: " + plugin_path.string();
      return false;
    }

#if defined(_WIN32)
    const auto path_value = plugin_path.string();
    auto* module = LoadLibraryA(path_value.c_str());
    if (module == nullptr) {
      error = "Failed to load plugin: " + path_value;
      return false;
    }

    auto register_fn =
        reinterpret_cast<cmdsdk::RegisterCommandsFn>(GetProcAddress(module, "RegisterCommands"));
    if (register_fn == nullptr) {
      error = "RegisterCommands symbol not found in plugin: " + path_value;
      FreeLibrary(module);
      return false;
    }
#else
    auto* module = dlopen(plugin_path.c_str(), RTLD_NOW);
    if (module == nullptr) {
      const auto* dl_error = dlerror();
      error = "Failed to load plugin: " + plugin_path.string() +
              (dl_error != nullptr ? (" (" + std::string(dl_error) + ")") : std::string());
      return false;
    }

    auto register_fn = reinterpret_cast<cmdsdk::RegisterCommandsFn>(dlsym(module, "RegisterCommands"));
    if (register_fn == nullptr) {
      const auto* dl_error = dlerror();
      error = "RegisterCommands symbol not found in plugin: " + plugin_path.string() +
              (dl_error != nullptr ? (" (" + std::string(dl_error) + ")") : std::string());
      dlclose(module);
      return false;
    }
#endif

    register_fn(registry_);
    loaded_plugins_.push_back({plugin_path, module});
    return true;
  }

  json listCommands() const {
    json items = json::array();
    for (const auto& meta : registry_.listMetadata()) {
      json metadata;
      metadata["plugin_name"] = meta.plugin_name;
      metadata["cmd_name"] = meta.cmd_name;
      metadata["description"] = meta.description;
      metadata["is_tool"] = meta.is_tool;
      metadata["is_app_tool"] = meta.is_app_tool;
      metadata["resource_uri"] = meta.resource_uri;

      json parameters = json::array();
      for (const auto& param : meta.parameters) {
        parameters.push_back({
            {"parameter_name", param.parameter_name},
            {"parameter_type", param.parameter_type},
            {"required", param.required},
            {"validation", param.validation},
            {"description", param.description},
        });
      }
      metadata["parameters"] = std::move(parameters);

      json subtypes = json::array();
      for (const auto& subtype : meta.sub_cmd_types) {
        subtypes.push_back({
            {"sub_type_name", subtype.sub_type_name},
            {"description", subtype.description},
            {"response_schema", subtype.response_schema},
        });
      }
      metadata["sub_cmd_types"] = std::move(subtypes);

      items.push_back(std::move(metadata));
    }

    json result;
    result["count"] = items.size();
    result["commands"] = std::move(items);
    return result;
  }

  json execute(const std::string& session_id,
               const std::string& command_name,
               const json& args,
               std::string& error) {
    auto cmd = registry_.create(command_name);
    if (!cmd) {
      error = "Command not found: " + command_name;
      return json::object();
    }

    std::string command_error;
    if (!cmd->validate(args, command_error)) {
      error = "Validation failed: " + command_error;
      return json::object();
    }

    if (!cmd->execute(args, command_error)) {
      error = "Execution failed: " + command_error;
      return json::object();
    }

    json command_result = cmd->getResult();
    SessionContext& session = sessions_[session_id];
    session.session_id = session_id;
    session.command_count += 1;
    session.last_command = command_name;
    session.last_arguments = args;
    session.last_result = command_result;

    session.history.push_back({
        {"command", command_name},
        {"arguments", args},
        {"result", command_result},
        {"index", session.command_count},
    });

    if (session.history.size() > 50) {
      session.history.erase(session.history.begin(), session.history.begin() + (session.history.size() - 50));
    }

    return command_result;
  }

  json getSession(const std::string& session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
      return {
          {"session_id", session_id},
          {"command_count", 0},
          {"last_command", ""},
          {"last_arguments", json::object()},
          {"last_result", json::object()},
          {"history", json::array()},
      };
    }

    const SessionContext& session = it->second;
    return {
        {"session_id", session.session_id},
        {"command_count", session.command_count},
        {"last_command", session.last_command},
        {"last_arguments", session.last_arguments},
        {"last_result", session.last_result},
        {"history", session.history},
    };
  }

  void resetSession(const std::string& session_id) { sessions_.erase(session_id); }

  void unloadAll() {
    for (auto& plugin : loaded_plugins_) {
      if (plugin.handle == nullptr) {
        continue;
      }
#if defined(_WIN32)
      FreeLibrary(reinterpret_cast<HMODULE>(plugin.handle));
#else
      dlclose(plugin.handle);
#endif
      plugin.handle = nullptr;
    }
    loaded_plugins_.clear();
  }

 private:
  cmdsdk::CommandRegistry registry_;
  std::vector<LoadedPlugin> loaded_plugins_;
  std::unordered_map<std::string, SessionContext> sessions_;
};

std::mutex g_mutex;
std::string g_last_error;
std::unique_ptr<Runtime> g_runtime;

const char* dupCString(const std::string& value) {
  const std::size_t length = value.size();
  auto* buffer = static_cast<char*>(std::malloc(length + 1));
  if (buffer == nullptr) {
    return nullptr;
  }

  if (length > 0) {
    std::memcpy(buffer, value.data(), length);
  }
  buffer[length] = '\0';
  return buffer;
}

void setLastError(const std::string& value) { g_last_error = value; }

std::string toSessionId(const char* value) {
  if (value == nullptr || *value == '\0') {
    return "default";
  }
  return value;
}

bool ensureRuntime() {
  if (!g_runtime) {
    g_runtime = std::make_unique<Runtime>();
  }
  return true;
}

}  // namespace

extern "C" CMDSDK_BRIDGE_API int cmdsdk_bridge_init() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();
  setLastError("");
  return 0;
}

extern "C" CMDSDK_BRIDGE_API int cmdsdk_bridge_shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_runtime.reset();
  setLastError("");
  return 0;
}

extern "C" CMDSDK_BRIDGE_API int cmdsdk_bridge_load_plugin(const char* plugin_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();

  if (plugin_path == nullptr || *plugin_path == '\0') {
    setLastError("plugin_path cannot be empty");
    return 1;
  }

  std::string error;
  if (!g_runtime->loadPlugin(plugin_path, error)) {
    setLastError(error);
    return 1;
  }

  setLastError("");
  return 0;
}

extern "C" CMDSDK_BRIDGE_API int cmdsdk_bridge_load_plugins_csv(const char* plugin_csv) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();

  if (plugin_csv == nullptr || *plugin_csv == '\0') {
    setLastError("plugin_csv cannot be empty");
    return 1;
  }

  std::string list = plugin_csv;
  std::size_t offset = 0;
  while (offset < list.size()) {
    std::size_t delimiter = list.find_first_of(",;", offset);
    const std::string token =
        (delimiter == std::string::npos) ? list.substr(offset) : list.substr(offset, delimiter - offset);

    if (!token.empty()) {
      std::string error;
      if (!g_runtime->loadPlugin(token, error)) {
        setLastError(error);
        return 1;
      }
    }

    if (delimiter == std::string::npos) {
      break;
    }
    offset = delimiter + 1;
  }

  setLastError("");
  return 0;
}

extern "C" CMDSDK_BRIDGE_API const char* cmdsdk_bridge_list_commands_json() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();

  try {
    const std::string payload = g_runtime->listCommands().dump();
    setLastError("");
    return dupCString(payload);
  } catch (const std::exception& ex) {
    setLastError(ex.what());
    return dupCString("{}");
  }
}

extern "C" CMDSDK_BRIDGE_API const char* cmdsdk_bridge_execute_json(const char* session_id,
                                                                      const char* command_name,
                                                                      const char* args_json) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();

  if (command_name == nullptr || *command_name == '\0') {
    setLastError("command_name cannot be empty");
    return dupCString("{}");
  }

  json args = json::object();
  if (args_json != nullptr && *args_json != '\0') {
    try {
      args = json::parse(args_json);
      if (!args.is_object()) {
        setLastError("args_json must decode to a JSON object");
        return dupCString("{}");
      }
    } catch (const std::exception& ex) {
      setLastError(std::string("Invalid args_json: ") + ex.what());
      return dupCString("{}");
    }
  }

  std::string error;
  const std::string session = toSessionId(session_id);
  json result = g_runtime->execute(session, command_name, args, error);
  if (!error.empty()) {
    setLastError(error);
    return dupCString("{}");
  }

  json envelope;
  envelope["session_id"] = session;
  envelope["command"] = command_name;
  envelope["result"] = std::move(result);
  envelope["session"] = g_runtime->getSession(session);

  setLastError("");
  return dupCString(envelope.dump());
}

extern "C" CMDSDK_BRIDGE_API const char* cmdsdk_bridge_get_session_state_json(const char* session_id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();

  const std::string session = toSessionId(session_id);
  const std::string payload = g_runtime->getSession(session).dump();
  setLastError("");
  return dupCString(payload);
}

extern "C" CMDSDK_BRIDGE_API void cmdsdk_bridge_reset_session(const char* session_id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensureRuntime();

  g_runtime->resetSession(toSessionId(session_id));
  setLastError("");
}

extern "C" CMDSDK_BRIDGE_API const char* cmdsdk_bridge_last_error() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return dupCString(g_last_error);
}

extern "C" CMDSDK_BRIDGE_API void cmdsdk_bridge_free_string(const char* value) {
  if (value != nullptr) {
    std::free(const_cast<char*>(value));
  }
}
