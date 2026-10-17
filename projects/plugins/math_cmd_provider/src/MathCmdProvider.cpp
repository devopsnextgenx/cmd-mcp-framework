#include <iostream>
#include <memory>
#include <string>
#include <cmath>
#include <functional>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "cmdsdk/CommandMetadata.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"
#include "cmdsdk/SubCmd.hpp"
#include "cmdsdk/ProviderRegistrar.hpp"

namespace {

// Floating-point epsilon for near-zero divisor checks.
// A raw (right == 0) comparison is unreliable for doubles: arithmetic such as
// (1.0/3.0 * 3.0 - 1.0) yields ~5.5e-17 instead of 0.  Values within
// [-kDivEpsilon, kDivEpsilon] are treated as effectively zero.
static constexpr double kDivEpsilon = 1e-9;

nlohmann::json mathResultSchema(const std::string& subtype_name,
                                 const std::string& operation,
                                 bool nullable_value = false) {
  nlohmann::json value_schema = {{"type", "number"}};
  if (nullable_value) {
    value_schema = {{"type", nlohmann::json::array({"number", "null"})}};
  }

  return {
      {"type", "object"},
      {"required", nlohmann::json::array({"subTypeExecuted", "operation", "value"})},
      {"properties", {
          {"subTypeExecuted", {{"type", "string"}, {"const", subtype_name}}},
          {"operation", {{"type", "string"}, {"const", operation}}},
          {"value", value_schema}
      }}
  };
}

class MathCmdProvider final : public cmdsdk::SubCmd {
 public:
  // Handler type for the dispatcher map.
  using Handler = std::function<bool(MathCmdProvider*, double, double)>;

  MathCmdProvider() : cmdsdk::SubCmd() {
    setPluginName("MATH");
    registerSubCmdType("MATH.ADD", {"MATH.ADD", "Add left and right."});
    registerSubCmdType("MATH.SUB", {"MATH.SUB", "Subtract right from left."});
    registerSubCmdType("MATH.MUL", {"MATH.MUL", "Multiply left and right."});
    registerSubCmdType("MATH.DIV", {"MATH.DIV", "Divide left by right."});
    registerSubCmdType("MATH.MOD", {"MATH.MOD", "Modulo of left by right."});
    registerSubCmdType("MATH.POW", {"MATH.POW", "Raise left to the power of right."});

    // Dispatcher map: built once at construction, looked up in O(1) in execute().
    // To add a new operation: register its subtype above, add a handler here,
    // and implement a private executeXxx() method below.
    dispatcher_ = {
      {"MATH.ADD", [](MathCmdProvider* s, double l, double r){ return s->executeAdd(l, r); }},
      {"MATH.SUB", [](MathCmdProvider* s, double l, double r){ return s->executeSub(l, r); }},
      {"MATH.MUL", [](MathCmdProvider* s, double l, double r){ return s->executeMul(l, r); }},
      {"MATH.DIV", [](MathCmdProvider* s, double l, double r){ return s->executeDiv(l, r); }},
      {"MATH.MOD", [](MathCmdProvider* s, double l, double r){ return s->executeMod(l, r); }},
      {"MATH.POW", [](MathCmdProvider* s, double l, double r){ return s->executePow(l, r); }},
    };
  }

  cmdsdk::CommandMetadata buildMetadata() const override {
    cmdsdk::CommandMetadata metadata;
    metadata.cmd_name   = "math.calculate";
    metadata.description =
        "Math command provider extending abstract Cmd. Demonstrates subtype "
        "registration with dispatcher-map execution.";
    metadata.parameters = {
        {"subType", "string", true,
         "Allowed values: MATH.ADD, MATH.SUB, MATH.MUL, MATH.DIV, MATH.MOD, MATH.POW.",
         "Selects which mathematical operation to perform."},
        {"left",  "number", true, "Must be numeric.", "Left operand."},
        {"right", "number", true, "Must be numeric.", "Right operand."},
    };
    metadata.sub_cmd_types = {
      {"MATH.ADD", "Add left and right.", mathResultSchema("MATH.ADD", "addition")},
      {"MATH.SUB", "Subtract right from left.", mathResultSchema("MATH.SUB", "subtraction")},
      {"MATH.MUL", "Multiply left and right.", mathResultSchema("MATH.MUL", "multiplication")},
      {"MATH.DIV", "Divide left by right.", mathResultSchema("MATH.DIV", "division", true)},
      {"MATH.MOD", "Modulo of left by right.", mathResultSchema("MATH.MOD", "modulo", true)},
      {"MATH.POW", "Raise left to the power of right.", mathResultSchema("MATH.POW", "power")},
    };
    metadata.is_app_tool = true;
    metadata.resource_uri = "ui://ui/math-form.html";
    return metadata;
  }

  bool validate(const nlohmann::json& input, std::string& error) override {
    if (!input.is_object()) {
      error = "Input must be a JSON object.";
      return false;
    }

    if (!input.contains("subType") || !input.at("subType").is_string()) {
      error = "Missing required string field: subType.";
      return false;
    }

    if (!input.contains("left") || !input.at("left").is_number()) {
      error = "Missing required numeric field: left.";
      return false;
    }

    if (!input.contains("right") || !input.at("right").is_number()) {
      error = "Missing required numeric field: right.";
      return false;
    }

    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());
    if (sub_type == cmdsdk::UNKNOWN_SUBCMD_TYPE) {
      error = "subType must be one of: MATH.ADD, MATH.SUB, MATH.MUL, MATH.DIV, MATH.MOD, MATH.POW.";
      return false;
    }

    return true;
  }

  // Dispatches to the correct handler in O(1) via the dispatcher map.
  // Adding a new operation only requires: a registerSubCmdType() call above,
  // a new dispatcher_ entry, and a new private executeXxx() method.
  bool execute(const nlohmann::json& input, std::string& error) override {
    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());
    const double left  = input.at("left").get<double>();
    const double right = input.at("right").get<double>();

    const auto it = dispatcher_.find(sub_type);
    if (it == dispatcher_.end()) {
      error = "Unsupported subtype requested.";
      return false;
    }

    return it->second(this, left, right);
  }

 private:
  std::unordered_map<std::string, Handler> dispatcher_;

  bool executeAdd(double left, double right) {
    setResult({
        {"subTypeExecuted", "MATH.ADD"},
        {"operation", "addition"},
        {"value", left + right},
    });
    return true;
  }

  bool executeSub(double left, double right) {
    setResult({
        {"subTypeExecuted", "MATH.SUB"},
        {"operation", "subtraction"},
        {"value", left - right},
    });
    return true;
  }

  bool executeMul(double left, double right) {
    setResult({
        {"subTypeExecuted", "MATH.MUL"},
        {"operation", "multiplication"},
        {"value", left * right},
    });
    return true;
  }

  // Epsilon guard: std::abs(right) < kDivEpsilon rather than right == 0
  // avoids false-positives from floating-point rounding noise and false-negatives
  // from tiny but non-zero values that would cause IEEE 754 infinity.
  bool executeDiv(double left, double right) {
    if (std::abs(right) < kDivEpsilon) {
      setResult({
          {"subTypeExecuted", "MATH.DIV"},
          {"operation", "division"},
          {"value", nullptr},  // divisor too close to zero – undefined result
      });
      return true;
    }
    setResult({
        {"subTypeExecuted", "MATH.DIV"},
        {"operation", "division"},
        {"value", left / right},
    });
    return true;
  }

  // Same epsilon guard: std::fmod is also undefined when |right| is effectively zero.
  bool executeMod(double left, double right) {
    if (std::abs(right) < kDivEpsilon) {
      setResult({
          {"subTypeExecuted", "MATH.MOD"},
          {"operation", "modulo"},
          {"value", nullptr},  // divisor too close to zero – undefined result
      });
      return true;
    }
    setResult({
        {"subTypeExecuted", "MATH.MOD"},
        {"operation", "modulo"},
        {"value", std::fmod(left, right)},
    });
    return true;
  }

  bool executePow(double left, double right) {
    setResult({
        {"subTypeExecuted", "MATH.POW"},
        {"operation", "power"},
        {"value", std::pow(left, right)},
    });
    return true;
  }
};

}  // namespace

// SIOF safety note:
// CMDSDK_REGISTER_PROVIDER expands to a file-static AutoRegister<T> whose
// constructor calls ProviderRegistrar::instance().addFactory(...).
// ProviderRegistrar::instance() returns a *function-local* static, which
// C++11 §6.7 guarantees is initialised exactly once, on first call, before
// any use – regardless of cross-TU static-init order.  This eliminates the
// Static Initialisation Order Fiasco even when multiple plugin shared libraries
// are loaded in unpredictable order by the plugin host.
CMDSDK_REGISTER_PROVIDER(MathCmdProvider);
