#include "cmdsdk/Cmd.hpp"
#include <utility>

namespace cmdsdk {

Cmd::Cmd() : result_(nlohmann::json::object()) {}

nlohmann::json Cmd::getResult() const { return result_; }

void Cmd::setResult(nlohmann::json result) { result_ = std::move(result); }

}  // namespace cmdsdk
