# ---------------------------------------------------------------------------
# fastmcp_server — CMakeLists.txt
#
# MCP layer     → cpp-mcp-sdk  (itcv-GmbH/cpp-mcp-sdk, C++17)
# REST layer    → cpp-httplib  (already fetched at root level)
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# ---------------------------------------------------------------------------
# Recompile control — inherits from root or can be set standalone
# ---------------------------------------------------------------------------
option(FORCE_RECOMPILE "Force re-download and recompile of all FetchContent dependencies" OFF)

if(FORCE_RECOMPILE)
  message(STATUS "FORCE_RECOMPILE=ON — wiping FetchContent stamps for mcp_sdk")
  file(REMOVE_RECURSE
    "${CMAKE_BINARY_DIR}/_deps/mcp_sdk-subbuild"
    "${CMAKE_BINARY_DIR}/_deps/mcp_sdk-src"
    "${CMAKE_BINARY_DIR}/_deps/mcp_sdk-build"
  )
endif()

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# ---------------------------------------------------------------------------
# cpp-mcp-sdk — cached by shallow clone; recompiles only if GIT_TAG changes
# ---------------------------------------------------------------------------
FetchContent_Declare(
  mcp_sdk
  GIT_REPOSITORY https://github.com/itcv-GmbH/cpp-mcp-sdk.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   FALSE
)

# Disable optional SDK components to minimize compile work
set(MCP_SDK_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MCP_SDK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MCP_SDK_ENABLE_TLS     OFF CACHE BOOL "" FORCE)
set(MCP_SDK_ENABLE_AUTH    OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING           OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(mcp_sdk)

# ---------------------------------------------------------------------------
# fastmcp_server executable
# ---------------------------------------------------------------------------

# Collect all source and header files; CMake will recompile only changed TUs.
file(GLOB_RECURSE FASTMCP_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE FASTMCP_HEADERS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_executable(fastmcp_server
  ${FASTMCP_SOURCES}
  ${FASTMCP_HEADERS}   # listed so IDEs and Ninja track header changes
)

# Mark headers as private so they participate in dependency tracking
set_target_properties(fastmcp_server PROPERTIES
  CXX_SCAN_FOR_MODULES OFF  # explicit header list above is sufficient
)

target_compile_features(fastmcp_server PRIVATE cxx_std_17)

target_link_libraries(fastmcp_server
  PRIVATE
    cmd_sdk
    mcp::sdk
    nlohmann_json::nlohmann_json
)

# cpp-httplib target variants across package managers / FetchContent
if(TARGET httplib::httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib::httplib)
elseif(TARGET httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib)
elseif(DEFINED cpp_httplib_SOURCE_DIR)
  # Header-only fallback: add include path; header changes tracked via FASTMCP_HEADERS glob
  target_include_directories(fastmcp_server PRIVATE ${cpp_httplib_SOURCE_DIR})
endif()

target_compile_definitions(fastmcp_server
  PRIVATE
    CMDSDK_SERVER_PORT=5432
    CMDSDK_SAMPLE_PLUGIN_NAME="${CMAKE_SHARED_LIBRARY_PREFIX}math_cmd_provider${CMAKE_SHARED_LIBRARY_SUFFIX}"
)