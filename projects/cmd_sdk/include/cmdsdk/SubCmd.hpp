#pragma once

#include <string>
#include <utility>
#include <vector>
#include "cmdsdk/CommandMetadata.hpp"

#include "cmdsdk/Cmd.hpp"

namespace cmdsdk {

enum class SubCmdType { TypeA, TypeB, TypeC, TypeD, TypeE, TypeF, Unknown };

class SubCmd : public Cmd {
 public:
  SubCmd();
  ~SubCmd() override = default;

  void registerSubCmdType(SubCmdType type, SubCmdTypeMetadata metadata);

 protected:
  SubCmdType resolveSubCmdType(const std::string& subtype_name) const;
  const std::vector<std::pair<SubCmdType, SubCmdTypeMetadata>>& registeredSubCmdTypes() const;

 private:
  std::vector<std::pair<SubCmdType, SubCmdTypeMetadata>> sub_cmd_types_;
};

}  // namespace cmdsdk
