#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "cmdsdk/CommandMetadata.hpp"
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"
#include "cmdsdk/SubCmd.hpp"
#include "cmdsdk/ProviderRegistrar.hpp"

namespace {

class GreetingCmdProvider final : public cmdsdk::SubCmd {
 public:
  GreetingCmdProvider() : cmdsdk::SubCmd() {
    setPluginName("HELLO");
  }

  cmdsdk::CommandMetadata buildMetadata() const override {
    cmdsdk::CommandMetadata metadata;
    metadata.cmd_name = "greeting.greet";
    metadata.description =
        "Generate a greeting for an optional name. The tool can also be opened as a UI form.";
    metadata.parameters = {
        {"name", "string", false, "Optional name to personalize the greeting.", "Name to greet."},
    };
    metadata.is_tool = true;
    metadata.is_app_tool = true;
    metadata.resource_uri = "ui://ui/greet.html";
    return metadata;
  }

  bool validate(const nlohmann::json& input, std::string& error) override {
    if (!input.is_object()) {
      error = "Input must be a JSON object.";
      return false;
    }

    if (input.contains("name") && !input.at("name").is_string()) {
      error = "Field 'name' must be a string when provided.";
      return false;
    }

    return true;
  }

  bool execute(const nlohmann::json& input, std::string& error) override {
    std::string name;
    if (input.contains("name") && input.at("name").is_string()) {
      name = input.at("name").get<std::string>();
    }

    const std::string trimmed = name.empty() ? "" : name;
    const std::string greeting = trimmed.empty() ? "Hello World!!!" : "Hello " + trimmed + "!!!";

    setResult({{"message", greeting}, {"name", name}});
    return true;
  }
};

}  // namespace

CMDSDK_REGISTER_PROVIDER(GreetingCmdProvider);
