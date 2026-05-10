cmake_minimum_required(VERSION 3.20)

project(cmd_mcp_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.16.3
)
FetchContent_MakeAvailable(cpp_httplib)

add_subdirectory(projects/cmd_sdk)
add_subdirectory(projects/plugins/math_cmd_provider)
add_subdirectory(projects/fastmcp_server)

find_program(NPM_EXECUTABLE npm)

if(NPM_EXECUTABLE)
    add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/mcp-apps/dashboard-ui/dist/index.html"
        COMMAND ${NPM_EXECUTABLE} install
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/mcp-apps/dashboard-ui"
        COMMENT "Building React MCP App..."
    )
    add_custom_target(build_ui ALL DEPENDS "${CMAKE_SOURCE_DIR}/mcp-apps/dashboard-ui/dist/index.html")
    # Ensure the server builds after the UI is ready
    add_dependencies(fastmcp_server build_ui)
endif()