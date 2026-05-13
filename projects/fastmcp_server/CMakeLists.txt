# ---------------------------------------------------------------------------
# fastmcp_server — CMakeLists.txt
#
# MCP layer  → cpp-mcp-sdk (itcv-GmbH/cpp-mcp-sdk, C++17)
#   Public CMake target: mcp::sdk
#
# REST/Swagger/proxy layer → cpp-httplib
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# ── cpp-mcp-sdk ──────────────────────────────────────────────────────────────
FetchContent_Declare(
  mcp_sdk
  GIT_REPOSITORY https://github.com/itcv-GmbH/cpp-mcp-sdk.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)

# Disable heavy optional components in SDK dependency build
set(MCP_SDK_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MCP_SDK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MCP_SDK_ENABLE_TLS     OFF CACHE BOOL "" FORCE)
set(MCP_SDK_ENABLE_AUTH    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(mcp_sdk)

# ── fastmcp_server ───────────────────────────────────────────────────────────
add_executable(fastmcp_server
  src/main.cpp
  src/PluginLoader.cpp
)

target_compile_features(fastmcp_server PRIVATE cxx_std_17)

target_link_libraries(fastmcp_server
  PRIVATE
    cmd_sdk
    mcp::sdk
    nlohmann_json::nlohmann_json
)

# cpp-httplib target variants across package managers.
if(TARGET httplib::httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib::httplib)
elseif(TARGET httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib)
elseif(DEFINED cpp_httplib_SOURCE_DIR)
  target_include_directories(fastmcp_server PRIVATE ${cpp_httplib_SOURCE_DIR})
endif()

target_compile_definitions(fastmcp_server
  PRIVATE
    CMDSDK_SERVER_PORT=5432
    CMDSDK_SAMPLE_PLUGIN_NAME="${CMAKE_SHARED_LIBRARY_PREFIX}math_cmd_provider${CMAKE_SHARED_LIBRARY_SUFFIX}"
)