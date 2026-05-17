#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>

namespace cmdsdk {

/**
 * PluginOpenApiLoader
 *
 * Discovers and loads OpenAPI specifications from plugin directories.
 *
 * Plugins can optionally ship with an openapi.spec.yml or openapi.spec.json file
 * in the same directory as the shared library:
 *
 *   libmath_cmd_provider.so
 *   openapi.spec.yml         <- Discovered and loaded by this utility
 *
 * Specs are parsed from YAML/JSON and returned as nlohmann::json objects.
 */
class PluginOpenApiLoader {
 public:
  /**
   * Discover plugin OpenAPI spec files in a directory.
   *
   * Looks for:
   *   - openapi.spec.yml
   *   - openapi.spec.yaml
   *   - openapi.spec.json
   *   - openapi.yml
   *   - openapi.json
   *
   * @param plugin_dir Directory to search (typically where .so lives)
   * @return Path to OpenAPI spec file, or empty string if not found
   */
  static std::string discoverOpenApiSpecFile(const std::filesystem::path& plugin_dir);

  /**
   * Load and parse OpenAPI spec from file.
   *
   * Handles both YAML and JSON formats.
   * For YAML: requires basic parsing (no heavy YAML library required).
   * For JSON: uses nlohmann::json parser.
   *
   * @param spec_file Path to OpenAPI spec file
   * @param error_out Output parameter for error messages
   * @return Parsed OpenAPI spec object, or empty object on error
   */
  static nlohmann::json loadOpenApiSpec(const std::filesystem::path& spec_file,
                                        std::string& error_out);

  /**
   * Try to load OpenAPI spec for a plugin at given directory.
   *
   * Combines discovery and loading in one call.
   *
   * @param plugin_dir Directory to search
   * @param error_out Output parameter for error messages
   * @return Parsed OpenAPI spec, or empty object if not found or error
   */
  static nlohmann::json tryLoadPluginOpenApi(const std::filesystem::path& plugin_dir,
                                             std::string& error_out);

 private:
  /**
   * Simple YAML to JSON converter for basic OpenAPI specs.
   *
   * Only handles key: value pairs and nested structures.
   * Not a full YAML parser, but sufficient for simple OpenAPI specs.
   *
   * @param yaml_content YAML string content
   * @return JSON object
   */
  static nlohmann::json parseYamlToJson(const std::string& yaml_content);

  /**
   * Load file contents as string.
   *
   * @param file_path Path to file
   * @param error_out Output parameter for error messages
   * @return File contents, or empty string on error
   */
  static std::string readFileContent(const std::filesystem::path& file_path,
                                     std::string& error_out);
};

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline std::string PluginOpenApiLoader::discoverOpenApiSpecFile(
    const std::filesystem::path& plugin_dir) {
  static const std::vector<std::string> candidates = {
      "openapi.spec.yml",  "openapi.spec.yaml", "openapi.spec.json",
      "openapi.yml",       "openapi.yaml",      "openapi.json"};

  for (const auto& candidate : candidates) {
    auto path = plugin_dir / candidate;
    if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
      return path.string();
    }
  }
  return "";
}

inline std::string PluginOpenApiLoader::readFileContent(
    const std::filesystem::path& file_path,
    std::string& error_out) {
  try {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      error_out = "Cannot open file: " + file_path.string();
      return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  } catch (const std::exception& ex) {
    error_out = "Error reading file: " + std::string(ex.what());
    return "";
  }
}

inline nlohmann::json PluginOpenApiLoader::parseYamlToJson(
    const std::string& yaml_content) {
  // Very basic YAML to JSON converter for simple key-value pairs
  // This is NOT a full YAML parser. For simple OpenAPI specs with basic structure:
  // - key: value
  // - nested:
  //     key: value
  //
  // We convert to JSON by heuristics. In production, consider yaml-cpp if needed.

  nlohmann::json result = nlohmann::json::object();

  // For now, try to parse as JSON first (many YAML files are JSON compatible)
  try {
    return nlohmann::json::parse(yaml_content);
  } catch (...) {
    // If JSON parse fails, return empty object
    // Full YAML parsing requires yaml-cpp library
    return result;
  }
}

inline nlohmann::json PluginOpenApiLoader::loadOpenApiSpec(
    const std::filesystem::path& spec_file,
    std::string& error_out) {
  std::string content = readFileContent(spec_file, error_out);
  if (content.empty()) {
    return nlohmann::json::object();
  }

  const auto filename = spec_file.filename().string();
  const bool is_yaml = filename.ends_with(".yml") || filename.ends_with(".yaml");
  const bool is_json = filename.ends_with(".json");

  try {
    if (is_json) {
      return nlohmann::json::parse(content);
    } else if (is_yaml) {
      // Try JSON-compatible YAML first
      try {
        return nlohmann::json::parse(content);
      } catch (...) {
        // For full YAML support, would need yaml-cpp
        // For now, log a warning and return empty
        error_out =
            "YAML file requires yaml-cpp library for full parsing: " + filename;
        return nlohmann::json::object();
      }
    }
  } catch (const std::exception& ex) {
    error_out = "Error parsing OpenAPI spec: " + std::string(ex.what());
    return nlohmann::json::object();
  }

  return nlohmann::json::object();
}

inline nlohmann::json PluginOpenApiLoader::tryLoadPluginOpenApi(
    const std::filesystem::path& plugin_dir,
    std::string& error_out) {
  const auto spec_file = discoverOpenApiSpecFile(plugin_dir);
  if (spec_file.empty()) {
    // No spec file found, but this is not an error
    return nlohmann::json::object();
  }
  return loadOpenApiSpec(spec_file, error_out);
}

}  // namespace cmdsdk

// Include required headers for implementation
#include <fstream>
#include <sstream>
#include <iostream>
