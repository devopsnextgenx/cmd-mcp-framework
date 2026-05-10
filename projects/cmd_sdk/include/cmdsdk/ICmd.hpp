#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace cmdsdk {

class ICmd {
 public:
  virtual ~ICmd() = default;

  virtual bool validate(const nlohmann::json& input, std::string& error) = 0;
  virtual bool execute(const nlohmann::json& input, std::string& error) = 0;
  virtual nlohmann::json getResult() const = 0;
};

}  // namespace cmdsdk
