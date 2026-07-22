add_library(cmdsdk_bridge SHARED
  src/CmdSdkBridge.cpp
)

target_include_directories(cmdsdk_bridge
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cmdsdk_bridge
  PRIVATE
    cmd_sdk
    nlohmann_json::nlohmann_json
)

set_target_properties(cmdsdk_bridge PROPERTIES OUTPUT_NAME "cmdsdk_bridge")
