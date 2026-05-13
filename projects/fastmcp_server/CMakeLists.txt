# ---------------------------------------------------------------------------
# fastmcp_server — CMakeLists.txt
#
# MCP layer  → cpp-mcp (hkr04/cpp-mcp, MIT, C++17)
#   Headers live FLAT under include/:
#     mcp_server.h, mcp_tool.h, mcp_resource.h, mcp_message.h …
#   CMake library target name: mcp
#   cpp-mcp itself FetchContents cpp-httplib and nlohmann/json.
#
# REST/Swagger/proxy layer → cpp-httplib (reused from cpp-mcp's fetch)
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# ── cpp-mcp ──────────────────────────────────────────────────────────────────
FetchContent_Declare(
  cpp_mcp
  GIT_REPOSITORY https://github.com/hkr04/cpp-mcp.git
  GIT_TAG        main     # pin to a sha for reproducible builds, e.g. GIT_TAG abc1234
  GIT_SHALLOW    TRUE
)

# Disable sub-targets we don't need so GoogleTest is not pulled in
set(MCP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MCP_SSL            OFF CACHE BOOL "" FORCE)  # set ON if you need HTTPS

FetchContent_MakeAvailable(cpp_mcp)
# After this call, cmake exposes:
#   cpp_mcp_SOURCE_DIR  — root of the checked-out repo
#   target: mcp         — the compiled library

# ── fastmcp_server ───────────────────────────────────────────────────────────
add_executable(fastmcp_server
  src/main.cpp
  src/PluginLoader.cpp
)

target_compile_features(fastmcp_server PRIVATE cxx_std_17)

target_link_libraries(fastmcp_server
  PRIVATE
    cmd_sdk
    mcp                           # cpp-mcp library (JSON-RPC, SSE/HTTP transport)
    nlohmann_json::nlohmann_json  # reuse the copy cpp-mcp already fetched
)

# cpp-httplib is fetched transitively by cpp-mcp; pick up whichever target name
# its CMakeLists exported.
if(TARGET httplib::httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib::httplib)
elseif(TARGET httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib)
elseif(DEFINED cpp_httplib_SOURCE_DIR)
  target_include_directories(fastmcp_server PRIVATE ${cpp_httplib_SOURCE_DIR})
endif()

# Explicitly add cpp-mcp's include directories.
# The 'mcp' target should propagate these, but we add them directly too for
# safety in case of older CMake or build system quirks.
target_include_directories(fastmcp_server
  PRIVATE
    ${cpp_mcp_SOURCE_DIR}/include  # mcp_server.h, mcp_tool.h, mcp_resource.h, …
    ${cpp_mcp_SOURCE_DIR}/common   # mcp_common.h, mcp_logger.h (if present)
)

target_compile_definitions(fastmcp_server
  PRIVATE
    CMDSDK_SERVER_PORT=5432
    CMDSDK_SAMPLE_PLUGIN_NAME="${CMAKE_SHARED_LIBRARY_PREFIX}math_cmd_provider${CMAKE_SHARED_LIBRARY_SUFFIX}"
)