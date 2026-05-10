#pragma once

#include <string>
#include <utility>
#include <vector>
#include "cmdsdk/CommandMetadata.hpp"

#include "cmdsdk/Cmd.hpp"

namespace cmdsdk {

using SubCmdType = std::string;

const std::string UNKNOWN_SUBCMD_TYPE = "";
const size_t MAX_SUBCMD_TYPE_LENGTH = 128;

class SubCmd : public Cmd {
 public:
  SubCmd();
  ~SubCmd() override = default;

  void registerSubCmdType(const SubCmdType& type, SubCmdTypeMetadata metadata);
  virtual CommandMetadata buildMetadata() const = 0;

 protected:
  void setPluginName(const std::string& name);
  SubCmdType resolveSubCmdType(const std::string& subtype_name) const;
  const std::vector<std::pair<SubCmdType, SubCmdTypeMetadata>>& registeredSubCmdTypes() const;

 private:
  std::string plugin_name_;
  std::vector<std::pair<SubCmdType, SubCmdTypeMetadata>> sub_cmd_types_;
};

}  // namespace cmdsdk
