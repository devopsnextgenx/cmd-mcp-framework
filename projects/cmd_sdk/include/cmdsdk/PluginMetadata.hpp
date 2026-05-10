#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace cmdsdk {

/**
 * PluginMetadata
 *
 * Optional metadata for a plugin, including OpenAPI specification information.
 * Plugins can populate this during initialization to provide explicit OpenAPI
 * documentation that overrides auto-generated specs.
 *
 * Usage (in plugin provider .cpp):
 *
 *   static PluginMetadata plugin_meta;
 *   plugin_meta.plugin_name = "MATH";
 *   plugin_meta.version = "1.0.0";
 *   plugin_meta.description = "Math operations plugin";
 *   plugin_meta.openapi_spec = loadOpenApiSpec("openapi.spec.yml");
 *
 */
struct PluginMetadata {
  std::string plugin_name;           // Name of the plugin (e.g., "MATH")
  std::string version = "1.0.0";     // Plugin version
  std::string description;           // Plugin description
  nlohmann::json openapi_spec;       // Optional explicit OpenAPI spec
  bool has_custom_openapi = false;   // Flag indicating custom spec is provided
};

/**
 * PluginMetadataRegistry
 *
 * Global registry for plugin metadata.
 * Plugins can register their metadata during initialization.
 *
 * Usage:
 *   PluginMetadataRegistry::instance().registerPluginMetadata(plugin_meta);
 */
class PluginMetadataRegistry {
 public:
  /**
   * Get the singleton instance.
   */
  static PluginMetadataRegistry& instance();

  /**
   * Register plugin metadata.
   *
   * @param metadata Plugin metadata to register
   */
  void registerPluginMetadata(const PluginMetadata& metadata);

  /**
   * Get metadata for a plugin.
   *
   * @param plugin_name Name of the plugin
   * @return Plugin metadata if found, empty struct otherwise
   */
  PluginMetadata getPluginMetadata(const std::string& plugin_name) const;

  /**
   * Check if a plugin has registered custom OpenAPI spec.
   *
   * @param plugin_name Name of the plugin
   * @return true if custom spec is available
   */
  bool hasCustomOpenApi(const std::string& plugin_name) const;

  /**
   * Get custom OpenAPI spec for plugin.
   *
   * @param plugin_name Name of the plugin
   * @return OpenAPI spec object if available, empty object otherwise
   */
  nlohmann::json getCustomOpenApi(const std::string& plugin_name) const;

  /**
   * Clear all registered metadata.
   */
  void clear();

 private:
  PluginMetadataRegistry() = default;

  struct MetadataEntry {
    PluginMetadata metadata;
  };

  std::map<std::string, MetadataEntry> registry_;
};

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline PluginMetadataRegistry& PluginMetadataRegistry::instance() {
  static PluginMetadataRegistry registry;
  return registry;
}

inline void PluginMetadataRegistry::registerPluginMetadata(
    const PluginMetadata& metadata) {
  registry_[metadata.plugin_name].metadata = metadata;
}

inline PluginMetadata PluginMetadataRegistry::getPluginMetadata(
    const std::string& plugin_name) const {
  auto it = registry_.find(plugin_name);
  if (it != registry_.end()) {
    return it->second.metadata;
  }
  return PluginMetadata{};
}

inline bool PluginMetadataRegistry::hasCustomOpenApi(
    const std::string& plugin_name) const {
  auto it = registry_.find(plugin_name);
  if (it != registry_.end()) {
    return it->second.metadata.has_custom_openapi &&
           !it->second.metadata.openapi_spec.is_null();
  }
  return false;
}

inline nlohmann::json PluginMetadataRegistry::getCustomOpenApi(
    const std::string& plugin_name) const {
  auto it = registry_.find(plugin_name);
  if (it != registry_.end() && it->second.metadata.has_custom_openapi) {
    return it->second.metadata.openapi_spec;
  }
  return nlohmann::json::object();
}

inline void PluginMetadataRegistry::clear() { registry_.clear(); }

}  // namespace cmdsdk

#include <map>
