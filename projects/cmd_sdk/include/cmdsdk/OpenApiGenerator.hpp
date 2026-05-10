#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "cmdsdk/CommandMetadata.hpp"

namespace cmdsdk {

/**
 * OpenApiGenerator
 *
 * Converts CommandMetadata to OpenAPI 3.0 compliant schemas.
 * Reuses the same type mapping and schema generation logic used for MCP tools.
 */
class OpenApiGenerator {
 public:
  /**
   * Convert a single CommandMetadata to an OpenAPI path item.
   *
   * Example output:
   *   POST /api/math.calculate
   *     requestBody: { content: { application/json: { schema: ... } } }
   *     responses: { 200: { content: { application/json: { schema: ... } } } }
   *
   * @param metadata CommandMetadata for the command
   * @param plugin_name Name of the plugin (for grouping in tags)
   * @return OpenAPI path item object
   */
  static nlohmann::json commandMetadataToOpenApiPathItem(
      const CommandMetadata& metadata,
      const std::string& plugin_name);

  /**
   * Convert parameter metadata to OpenAPI schema property.
   *
   * Handles type mapping:
   *   - "string"  -> type: "string"
   *   - "number"  -> type: "number"
   *   - "boolean" -> type: "boolean"
   *   - "object"  -> type: "object"
   *   - "array"   -> type: "array"
   *
   * For subType parameters with available subtypes, adds enum constraint.
   *
   * @param parameter ParameterMetadata
   * @param subtype_options Array of available subtype names, or nullptr
   * @return OpenAPI schema property object
   */
  static nlohmann::json parameterToOpenApiSchema(
      const ParameterMetadata& parameter,
      const nlohmann::json& subtype_options = nullptr);

  /**
   * Generate minimal OpenAPI 3.0 info object.
   *
   * @param title API title
   * @param version API version
   * @param description Optional description
   * @return OpenAPI info object
   */
  static nlohmann::json createOpenApiInfo(
      const std::string& title,
      const std::string& version,
      const std::string& description = "");

  /**
   * Generate OpenAPI request body schema from CommandMetadata.
   *
   * Creates a JSON Schema for POST request body with all parameters.
   *
   * @param metadata CommandMetadata
   * @param subtype_enums Array of available subtypes
   * @return OpenAPI requestBody object
   */
  static nlohmann::json createOpenApiRequestBody(
      const CommandMetadata& metadata,
      const nlohmann::json& subtype_enums);

  /**
   * Generate OpenAPI response schema.
   *
    * Returns subtype-aware schema when sub_cmd_types carry response_schema.
    * Falls back to generic result object when subtype response metadata is absent.
   *
    * @param metadata Command metadata with optional per-subtype response schemas
   * @return OpenAPI responses object (200 + error responses)
   */
    static nlohmann::json createOpenApiResponses(const CommandMetadata& metadata);

 private:
  static std::string mapParameterTypeToOpenApi(const std::string& cmdsdk_type);
};

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline std::string OpenApiGenerator::mapParameterTypeToOpenApi(
    const std::string& cmdsdk_type) {
  if (cmdsdk_type == "number") return "number";
  if (cmdsdk_type == "boolean") return "boolean";
  if (cmdsdk_type == "object") return "object";
  if (cmdsdk_type == "array") return "array";
  return "string";  // default
}

inline nlohmann::json OpenApiGenerator::parameterToOpenApiSchema(
    const ParameterMetadata& parameter,
    const nlohmann::json& subtype_options) {
  std::string type = mapParameterTypeToOpenApi(parameter.parameter_type);

  nlohmann::json schema = nlohmann::json::object();
  schema["type"] = type;
  schema["description"] = parameter.description;

  // For subType parameters with available options, add enum
  if (parameter.parameter_name == "subType" &&
      subtype_options != nullptr && subtype_options.is_array()) {
    schema["enum"] = subtype_options;
  }

  return schema;
}

inline nlohmann::json OpenApiGenerator::createOpenApiInfo(
    const std::string& title,
    const std::string& version,
    const std::string& description) {
  nlohmann::json info = nlohmann::json::object();
  info["title"] = title;
  info["version"] = version;
  if (!description.empty()) {
    info["description"] = description;
  }
  return info;
}

inline nlohmann::json OpenApiGenerator::createOpenApiRequestBody(
    const CommandMetadata& metadata,
    const nlohmann::json& subtype_enums) {
  nlohmann::json properties = nlohmann::json::object();
  nlohmann::json required = nlohmann::json::array();

  for (const auto& param : metadata.parameters) {
    nlohmann::json param_schema =
        (param.parameter_name == "subType" && !subtype_enums.empty())
            ? parameterToOpenApiSchema(param, subtype_enums)
            : parameterToOpenApiSchema(param);
    properties[param.parameter_name] = param_schema;
    if (param.required) {
      required.push_back(param.parameter_name);
    }
  }

  nlohmann::json schema = nlohmann::json::object();
  schema["type"] = "object";
  schema["properties"] = properties;
  schema["required"] = required;

  nlohmann::json content = nlohmann::json::object();
  content["application/json"]["schema"] = schema;

  nlohmann::json body = nlohmann::json::object();
  body["required"] = true;
  body["content"] = content;

  return body;
}

inline nlohmann::json OpenApiGenerator::createOpenApiResponses(
    const CommandMetadata& metadata) {
  // Success response (200)
  nlohmann::json response_200 = nlohmann::json::object();
  response_200["description"] = "Command executed successfully";

  nlohmann::json subtype_schemas = nlohmann::json::array();
  for (const auto& subtype : metadata.sub_cmd_types) {
    if (subtype.response_schema.is_object() && !subtype.response_schema.empty()) {
      subtype_schemas.push_back(subtype.response_schema);
    }
  }

  nlohmann::json success_schema = nlohmann::json::object();
  if (!subtype_schemas.empty()) {
    success_schema["oneOf"] = subtype_schemas;
    success_schema["description"] = "Subtype-specific command result";
  } else {
    success_schema["type"] = "object";
    success_schema["properties"]["result"] = {
        {"type", "object"},
        {"description", "Command result"}
    };
  }

  response_200["content"]["application/json"]["schema"] = success_schema;

  // Error response (400/500)
  nlohmann::json response_error = nlohmann::json::object();
  response_error["description"] = "Error executing command";
  nlohmann::json error_schema = nlohmann::json::object();
  error_schema["type"] = "object";
  error_schema["properties"]["error"] = {
      {"type", "string"},
      {"description", "Error message"}
  };
  response_error["content"]["application/json"]["schema"] = error_schema;

  nlohmann::json responses = nlohmann::json::object();
  responses["200"] = response_200;
  responses["400"] = response_error;
  responses["500"] = response_error;

  return responses;
}

inline nlohmann::json OpenApiGenerator::commandMetadataToOpenApiPathItem(
    const CommandMetadata& metadata,
    const std::string& plugin_name) {
  // Build subtype enum array
  nlohmann::json subtype_enums = nlohmann::json::array();
  for (const auto& subtype : metadata.sub_cmd_types) {
    subtype_enums.push_back(subtype.sub_type_name);
  }

  // Create POST operation
  nlohmann::json operation = nlohmann::json::object();
  operation["summary"] = metadata.description;
  operation["description"] = metadata.description;
  operation["operationId"] = metadata.cmd_name;
  operation["tags"] = nlohmann::json::array({plugin_name});
  operation["requestBody"] = createOpenApiRequestBody(metadata, subtype_enums);
  operation["responses"] = createOpenApiResponses(metadata);

  nlohmann::json path_item = nlohmann::json::object();
  path_item["post"] = operation;

  return path_item;
}

}  // namespace cmdsdk
