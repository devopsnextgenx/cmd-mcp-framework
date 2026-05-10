#pragma once

#include "cmdsdk/ICmd.hpp"

namespace cmdsdk {

class Cmd : public ICmd {
 public:
  Cmd();
  ~Cmd() override = default;

  nlohmann::json getResult() const override;

 protected:
  void setResult(nlohmann::json result);

 private:
  nlohmann::json result_;
};

}  // namespace cmdsdk
