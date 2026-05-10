#include <iostream>
#include <memory>
#include <string>
#include <cmath>

#include <nlohmann/json.hpp>

#include "cmdsdk/CommandMetadata.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"
#include "cmdsdk/SubCmd.hpp"
#include "cmdsdk/ProviderRegistrar.hpp"

namespace {
class MathCmdProvider final : public cmdsdk::SubCmd {
 public:
  MathCmdProvider() : cmdsdk::SubCmd() {
    setPluginName("MATH");
    registerSubCmdType("MATH.ADD", {"MATH.ADD", "Add left and right."});
    registerSubCmdType("MATH.SUB", {"MATH.SUB", "Subtract right from left."});
    registerSubCmdType("MATH.MUL", {"MATH.MUL", "Multiply left and right."});
    registerSubCmdType("MATH.DIV", {"MATH.DIV", "Divide left by right."});
    registerSubCmdType("MATH.MOD", {"MATH.MOD", "Modulo of left by right."});
    registerSubCmdType("MATH.POW", {"MATH.POW", "Raise left to the power of right."});
  }

  cmdsdk::CommandMetadata buildMetadata() const override {
    cmdsdk::CommandMetadata metadata;
    metadata.cmd_name   = "math.calculate";
    metadata.description =
        "Math command provider extending abstract Cmd. Demonstrates subtype "
        "registration with switch-case execution.";
    metadata.parameters = {
        {"subType", "string", true,
         "Allowed values: MATH.ADD, MATH.SUB, MATH.MUL, MATH.DIV, MATH.MOD, MATH.POW.",
         "Selects which mathematical operation to perform."},
        {"left",  "number", true, "Must be numeric.", "Left operand."},
        {"right", "number", true, "Must be numeric.", "Right operand."},
    };
    metadata.sub_cmd_types = {
        {"MATH.ADD", "Add left and right."},
        {"MATH.SUB", "Subtract right from left."},
        {"MATH.MUL", "Multiply left and right."},
        {"MATH.DIV", "Divide left by right."},
        {"MATH.MOD", "Modulo of left by right."},
        {"MATH.POW", "Raise left to the power of right."},
    };
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

  bool execute(const nlohmann::json& input, std::string& error) override {
    const auto sub_type = resolveSubCmdType(input.at("subType").get<std::string>());
    const double left = input.at("left").get<double>();
    const double right = input.at("right").get<double>();

    if (sub_type == "MATH.ADD") {
      return executeAdd(left, right);
    } else if (sub_type == "MATH.SUB") {
      return executeSub(left, right);
    } else if (sub_type == "MATH.MUL") {
      return executeMul(left, right);
    } else if (sub_type == "MATH.DIV") {
      return executeDiv(left, right);
    } else if (sub_type == "MATH.MOD") {
      return executeMod(left, right);
    } else if (sub_type == "MATH.POW") {
      return executePow(left, right);
    } else {
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

CMDSDK_REGISTER_PROVIDER(MathCmdProvider);
