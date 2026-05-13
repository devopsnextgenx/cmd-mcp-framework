add_library(cmd_sdk SHARED
  src/Cmd.cpp
  src/SubCmd.cpp
  src/CommandRegistry.cpp
  src/ProviderRegistrar.cpp
)

set(CMDSDK_REGISTRAR_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ProviderRegistrar.cpp
    CACHE INTERNAL "ProviderRegistrar source required by every plugin"
)

target_include_directories(cmd_sdk
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cmd_sdk
  PUBLIC
    nlohmann_json::nlohmann_json
)

target_compile_definitions(cmd_sdk
  PRIVATE
    CMDSDK_BUILDING_LIBRARY
)
