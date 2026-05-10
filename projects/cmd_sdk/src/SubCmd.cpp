#include "cmdsdk/SubCmd.hpp"

#include <algorithm>
#include <utility>

namespace cmdsdk {

SubCmd::SubCmd() : Cmd() {}

void SubCmd::registerSubCmdType(SubCmdType type, SubCmdTypeMetadata metadata) {
  const auto it = std::find_if(
      sub_cmd_types_.begin(), sub_cmd_types_.end(),
      [type](const std::pair<SubCmdType, SubCmdTypeMetadata>& existing) { return existing.first == type; });

  if (it == sub_cmd_types_.end()) {
    sub_cmd_types_.emplace_back(type, std::move(metadata));
  } else {
    it->second = std::move(metadata);
  }

}

SubCmdType SubCmd::resolveSubCmdType(const std::string& subtype_name) const {
  for (const auto& entry : sub_cmd_types_) {
    if (entry.second.sub_type_name == subtype_name) {
      return entry.first;
    }
  }
  return SubCmdType::Unknown;
}

const std::vector<std::pair<SubCmdType, SubCmdTypeMetadata>>& SubCmd::registeredSubCmdTypes() const {
  return sub_cmd_types_;
}

}  // namespace cmdsdk
