#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "cmdsdk/CommandMetadata.hpp"

#include "cmdsdk/ICmd.hpp"

namespace cmdsdk {

class CommandRegistry {
 public:
  using CommandFactory = std::function<std::unique_ptr<ICmd>()>;

  bool registerCommand(CommandMetadata metadata, CommandFactory factory, std::string& error);
  std::unique_ptr<ICmd> create(const std::string& cmd_name) const;
  bool hasCommand(const std::string& cmd_name) const;
  std::vector<CommandMetadata> listMetadata() const;

 private:
  struct RegisteredCommand {
    CommandMetadata metadata;
    CommandFactory factory;
  };

  std::unordered_map<std::string, RegisteredCommand> registry_;
};

}  // namespace cmdsdk
