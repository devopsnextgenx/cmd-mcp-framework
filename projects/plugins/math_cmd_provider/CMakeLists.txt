add_library(math_cmd_provider SHARED
  src/MathCmdProvider.cpp
)

target_link_libraries(math_cmd_provider
  PRIVATE
    cmd_sdk
    nlohmann_json::nlohmann_json
)

set_target_properties(math_cmd_provider PROPERTIES OUTPUT_NAME "math_cmd_provider")
