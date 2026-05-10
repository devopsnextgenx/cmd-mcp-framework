#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "cmdsdk/CommandRegistry.hpp"

namespace cmdsdk {

/**
 * RestApiHandler
 *
 * Handles REST API endpoint calls.
 * Converts REST requests into internal command execution via the CommandRegistry.
 *
 * Pattern:
 *   POST /api/{command_name}
 *   Content-Type: application/json
 *   Body: { "param1": value1, "param2": value2, ... }
 */
class RestApiHandler {
 public:
  /**
   * Execute a command via REST.
   *
   * @param command_name Name of the command to execute
   * @param arguments JSON object with command parameters
   * @param registry CommandRegistry to execute against
   * @param error_out Output parameter for error messages
   * @return JSON response with result or error
   */
  static nlohmann::json executeCommand(const std::string& command_name,
                                       const nlohmann::json& arguments,
                                       CommandRegistry& registry,
                                       std::string& error_out);

  /**
   * Build REST response envelope.
   *
   * Wraps command result in a standard REST response format.
   *
   * @param success Whether command succeeded
   * @param data Command result or error message
   * @return JSON response object
   */
  static nlohmann::json buildResponse(bool success, const nlohmann::json& data);

 private:
  static nlohmann::json buildErrorResponse(const std::string& error_msg);
};

// ============================================================================
// INLINE IMPLEMENTATIONS
// ============================================================================

inline nlohmann::json RestApiHandler::buildResponse(bool success,
                                                    const nlohmann::json& data) {
  nlohmann::json response = nlohmann::json::object();
  response["success"] = success;
  if (success) {
    response["result"] = data;
  } else {
    response["error"] = data;
  }
  return response;
}

inline nlohmann::json RestApiHandler::buildErrorResponse(
    const std::string& error_msg) {
  return buildResponse(false, error_msg);
}

inline nlohmann::json RestApiHandler::executeCommand(
    const std::string& command_name,
    const nlohmann::json& arguments,
    CommandRegistry& registry,
    std::string& error_out) {
  // Check if command exists
  if (!registry.hasCommand(command_name)) {
    error_out = "Command not found: " + command_name;
    return buildErrorResponse(error_out);
  }

  // Create command instance
  auto command = registry.create(command_name);
  if (!command) {
    error_out = "Failed to create command: " + command_name;
    return buildErrorResponse(error_out);
  }

  // Validate input
  if (!command->validate(arguments, error_out)) {
    return buildErrorResponse("Validation failed: " + error_out);
  }

  // Execute command
  if (!command->execute(arguments, error_out)) {
    return buildErrorResponse("Execution failed: " + error_out);
  }

  // Return result
  return buildResponse(true, command->getResult());
}

}  // namespace cmdsdk
