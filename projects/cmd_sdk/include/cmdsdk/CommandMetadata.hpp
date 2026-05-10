#pragma once

#include <string>
#include <vector>

namespace cmdsdk {

struct ParameterMetadata {
  std::string parameter_name;
  std::string parameter_type;
  bool required{true};
  std::string validation;
  std::string description;
};

struct SubCmdTypeMetadata {
  std::string sub_type_name;
  std::string description;
};

struct CommandMetadata {
  std::string cmd_name;
  std::string description;
  std::vector<ParameterMetadata> parameters;
  std::vector<SubCmdTypeMetadata> sub_cmd_types;
};

}  // namespace cmdsdk
