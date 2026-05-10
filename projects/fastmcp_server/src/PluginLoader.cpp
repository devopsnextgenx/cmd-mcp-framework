#include "PluginLoader.hpp"

#include <filesystem>

#include "cmdsdk/PluginApi.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

void closeHandle(void* handle) {
  if (handle == nullptr) {
    return;
  }

#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

}  // namespace

PluginLoader::~PluginLoader() {
  for (auto& plugin : loaded_plugins_) {
    closeHandle(plugin.handle);
    plugin.handle = nullptr;
  }
}

bool PluginLoader::load(const std::filesystem::path& plugin_path, cmdsdk::CommandRegistry& registry, std::string& error) {
  if (!std::filesystem::exists(plugin_path)) {
    error = "Plugin not found: " + plugin_path.string();
    return false;
  }

#if defined(_WIN32)
  const auto plugin_path_string = plugin_path.string();
  auto* module = LoadLibraryA(plugin_path_string.c_str());
  if (module == nullptr) {
    error = "Failed to load plugin: " + plugin_path_string;
    return false;
  }

  auto register_fn = reinterpret_cast<cmdsdk::RegisterCommandsFn>(GetProcAddress(module, "RegisterCommands"));
  if (register_fn == nullptr) {
    error = "RegisterCommands symbol not found in plugin: " + plugin_path_string;
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

  register_fn(registry);
  loaded_plugins_.push_back({plugin_path, module});
  return true;
}
