#include "cmdsdk/SubCmd.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace cmdsdk {

SubCmd::SubCmd() : Cmd() {}

void SubCmd::setPluginName(const std::string& name) {
  plugin_name_ = name;
}

void SubCmd::registerSubCmdType(const SubCmdType& type, SubCmdTypeMetadata metadata) {
  // Validate length
  if (type.length() > MAX_SUBCMD_TYPE_LENGTH) {
    std::cerr << "Warning: SubCmdType '" << type << "' exceeds maximum length of " << MAX_SUBCMD_TYPE_LENGTH << " characters.\n";
    return;
  }

  // Validate pattern: must start with plugin_name + "."
  if (!plugin_name_.empty()) {
    std::string expected_prefix = plugin_name_ + ".";
    if (type.find(expected_prefix) != 0) {
      std::cerr << "Warning: SubCmdType '" << type << "' does not follow the required pattern '" << plugin_name_ << ".<SubCmdType>'.\n";
      return;
    }
  }

  // Check for duplicates
  const auto it = std::find_if(
      sub_cmd_types_.begin(), sub_cmd_types_.end(),
      [&type](const std::pair<SubCmdType, SubCmdTypeMetadata>& existing) { return existing.first == type; });

  if (it != sub_cmd_types_.end()) {
    std::cerr << "Warning: SubCmdType '" << type << "' is already registered. Overwriting existing registration.\n";
    it->second = std::move(metadata);
  } else {
    sub_cmd_types_.emplace_back(type, std::move(metadata));
  }
}

SubCmdType SubCmd::resolveSubCmdType(const std::string& subtype_name) const {
  for (const auto& entry : sub_cmd_types_) {
    if (entry.second.sub_type_name == subtype_name) {
      return entry.first;
    }
  }
  return UNKNOWN_SUBCMD_TYPE;
}

const std::vector<std::pair<SubCmdType, SubCmdTypeMetadata>>& SubCmd::registeredSubCmdTypes() const {
  return sub_cmd_types_;
}

}  // namespace cmdsdk
