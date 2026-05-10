#include "cmdsdk/CommandRegistry.hpp"

#include <algorithm>

namespace cmdsdk {

bool CommandRegistry::registerCommand(CommandMetadata metadata, CommandFactory factory, std::string& error) {
  if (metadata.cmd_name.empty()) {
    error = "cmd_name cannot be empty during command registration.";
    return false;
  }

  if (!factory) {
    error = "Command factory cannot be empty.";
    return false;
  }

  if (registry_.contains(metadata.cmd_name)) {
    error = "Command is already registered: " + metadata.cmd_name;
    return false;
  }

  const std::string command_key = metadata.cmd_name;
  registry_.emplace(command_key, RegisteredCommand{std::move(metadata), std::move(factory)});
  return true;
}

std::unique_ptr<ICmd> CommandRegistry::create(const std::string& cmd_name) const {
  const auto it = registry_.find(cmd_name);
  if (it == registry_.end()) {
    return nullptr;
  }
  return it->second.factory();
}

bool CommandRegistry::hasCommand(const std::string& cmd_name) const { return registry_.contains(cmd_name); }

std::vector<CommandMetadata> CommandRegistry::listMetadata() const {
  std::vector<CommandMetadata> metadata;
  metadata.reserve(registry_.size());

  for (const auto& [cmd_name, command] : registry_) {
    (void)cmd_name;
    metadata.push_back(command.metadata);
  }

  std::sort(metadata.begin(), metadata.end(),
            [](const CommandMetadata& lhs, const CommandMetadata& rhs) { return lhs.cmd_name < rhs.cmd_name; });
  return metadata;
}

}  // namespace cmdsdk
