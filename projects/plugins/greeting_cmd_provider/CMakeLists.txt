add_library(greeting_cmd_provider SHARED
  src/GreetingCmdProvider.cpp
  ${CMDSDK_REGISTRAR_SRC}
)

target_link_libraries(greeting_cmd_provider
  PRIVATE
    cmd_sdk
    nlohmann_json::nlohmann_json
)

set_target_properties(greeting_cmd_provider PROPERTIES OUTPUT_NAME "greeting_cmd_provider")
