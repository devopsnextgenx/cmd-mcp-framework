#include <iostream>
#include <memory>
#include <string>
#include <cmath>

#include <nlohmann/json.hpp>

#include "cmdsdk/CommandMetadata.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"
#include "cmdsdk/SubCmd.hpp"

namespace {
cmdsdk::CommandMetadata buildMathMetadata() {
  cmdsdk::CommandMetadata metadata;
  metadata.cmd_name = "math.calculate";
  metadata.description =
      "Math command provider extending abstract Cmd. Demonstrates subtype registration with switch-case execution.";
  metadata.parameters = {
      {"subType", "string", true, "Allowed values: add, sub, mul, div, mod, pow.",
       "Selects which mathematical operation to perform."},
      {"left", "number", true, "Must be numeric.", "Left operand."},
      {"right", "number", true, "Must be numeric.", "Right operand."},
  };
  metadata.sub_cmd_types = {
      {"add", "Add left and right."},
      {"sub", "Subtract right from left."},
      {"mul", "Multiply left and right."},
      {"div", "Divide left by right."},
      {"mod", "Modulo of left by right."},
      {"pow", "Raise left to the power of right."},
  };
  return metadata;
}

class MathCmdProvider final : public cmdsdk::SubCmd {
 public:
  MathCmdProvider() : cmdsdk::SubCmd() {
    registerSubCmdType(cmdsdk::SubCmdType::TypeA, {"add", "Add left and right."});
    registerSubCmdType(cmdsdk::SubCmdType::TypeB, {"sub", "Subtract right from left."});
    registerSubCmdType(cmdsdk::SubCmdType::TypeC, {"mul", "Multiply left and right."});
    registerSubCmdType(cmdsdk::SubCmdType::TypeD, {"div", "Divide left by right."});
    registerSubCmdType(cmdsdk::SubCmdType::TypeE, {"mod", "Modulo of left by right."});
    registerSubCmdType(cmdsdk::SubCmdType::TypeF, {"pow", "Raise left to the power of right."});
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
    if (sub_type == cmdsdk::SubCmdType::Unknown) {
      error = "subType must be one of: add, sub, mul, div, mod, pow.";
      return false;
    }

    return true;
  }

  bool execute(const nlohmann::json& input, std::string& error) override {
    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());
    const double left = input.at("left").get<double>();
    const double right = input.at("right").get<double>();

    switch (sub_type) {
      case cmdsdk::SubCmdType::TypeA:
        return executeAdd(left, right);
      case cmdsdk::SubCmdType::TypeB:
        return executeSub(left, right);
      case cmdsdk::SubCmdType::TypeC:
        return executeMul(left, right);
      case cmdsdk::SubCmdType::TypeD:
        return executeDiv(left, right);
      case cmdsdk::SubCmdType::TypeE:
        return executeMod(left, right);
      case cmdsdk::SubCmdType::TypeF:
        return executePow(left, right);
      default:
        error = "Unsupported subtype requested.";
        return false;
    }
  }

 private:

  bool executeAdd(double left, double right) {
    setResult({
        {"subTypeExecuted", "add"},
        {"operation", "addition"},
        {"value", left + right},
    });
    return true;
  }

  bool executeSub(double left, double right) {
    setResult({
        {"subTypeExecuted", "sub"},
        {"operation", "subtraction"},
        {"value", left - right},
    });
    return true;
  }

  bool executeMul(double left, double right) {
    setResult({
        {"subTypeExecuted", "mul"},
        {"operation", "multiplication"},
        {"value", left * right},
    });
    return true;
  }

  bool executeDiv(double left, double right) {
    if (right == 0) {
      // Note: In a real implementation, you might want to handle division by zero
      setResult({
          {"subTypeExecuted", "div"},
          {"operation", "division"},
          {"value", nullptr},  // or some error indicator
      });
      return true;
    }
    setResult({
        {"subTypeExecuted", "div"},
        {"operation", "division"},
        {"value", left / right},
    });
    return true;
  }

  bool executeMod(double left, double right) {
    if (right == 0) {
      setResult({
          {"subTypeExecuted", "mod"},
          {"operation", "modulo"},
          {"value", nullptr},
      });
      return true;
    }
    setResult({
        {"subTypeExecuted", "mod"},
        {"operation", "modulo"},
        {"value", std::fmod(left, right)},
    });
    return true;
  }

  bool executePow(double left, double right) {
    setResult({
        {"subTypeExecuted", "pow"},
        {"operation", "power"},
        {"value", std::pow(left, right)},
    });
    return true;
  }
};

}  // namespace

extern "C" CMDSDK_API void RegisterCommands(cmdsdk::CommandRegistry& registry) {
  std::string error;
  if (!registry.registerCommand(buildMathMetadata(), []() -> std::unique_ptr<cmdsdk::ICmd> {
        return std::make_unique<MathCmdProvider>();
      }, error)) {
    std::cerr << "math_cmd_provider registration error: " << error << '\n';
  }
}
