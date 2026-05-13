#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cmdsdk/CommandRegistry.hpp"

class PluginLoader {
 public:
  ~PluginLoader();

  bool load(const std::filesystem::path& plugin_path, cmdsdk::CommandRegistry& registry, std::string& error);

 private:
  struct LoadedPlugin {
    std::filesystem::path path;
    void* handle;
  };

  std::vector<LoadedPlugin> loaded_plugins_;
};
