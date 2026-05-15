# ---------------------------------------------------------------------------
# root level — CMakeLists.txt
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

project(cmd_mcp_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Recompile control
# Set FORCE_RECOMPILE=ON to wipe fetched dependency stamps and re-download/build.
# ---------------------------------------------------------------------------
option(FORCE_RECOMPILE "Force re-download and recompile of all FetchContent dependencies" OFF)

include(FetchContent)

# Keep previously downloaded/configured sources across cmake re-runs.
# Sources won't be re-fetched unless the stamp is missing or FORCE_RECOMPILE=ON.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

if(FORCE_RECOMPILE)
  message(STATUS "FORCE_RECOMPILE=ON — wiping FetchContent stamps for nlohmann_json and cpp_httplib")
  file(REMOVE_RECURSE
    "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-subbuild"
    "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src"
    "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-build"
    "${CMAKE_BINARY_DIR}/_deps/cpp_httplib-subbuild"
    "${CMAKE_BINARY_DIR}/_deps/cpp_httplib-src"
    "${CMAKE_BINARY_DIR}/_deps/cpp_httplib-build"
  )
endif()

# ---------------------------------------------------------------------------
# nlohmann/json  — header-only, downloaded once and cached
# ---------------------------------------------------------------------------
FetchContent_Declare(
  nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)

# Disable nlohmann test/benchmark targets — they massively inflate build time
set(JSON_BuildTests   OFF CACHE BOOL "" FORCE)
set(JSON_Install      OFF CACHE BOOL "" FORCE)
set(JSON_MultipleHeaders OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# cpp-httplib — header-only, cached by git tag stamp
# ---------------------------------------------------------------------------
FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.16.3
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   FALSE
)

# Header-only: skip compiling the bundled example/test targets if present
set(HTTPLIB_COMPILE           OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_OPENSSL   OFF CACHE BOOL "" FORCE)
set(HTTPLIB_REQUIRE_ZLIB      OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING              OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cpp_httplib)

# ---------------------------------------------------------------------------
# Sub-projects
# ---------------------------------------------------------------------------
add_subdirectory(projects/cmd_sdk)
add_subdirectory(projects/plugins/math_cmd_provider)
add_subdirectory(projects/fastmcp_server)

# ---------------------------------------------------------------------------
# React UI build (mcp-apps/dashboard-ui)
# Uses vite-plugin-singlefile to produce embeddable HTML/JS/CSS artifact
# ---------------------------------------------------------------------------
find_program(NPM_EXECUTABLE npm)
find_program(NODE_EXECUTABLE node)

if(NPM_EXECUTABLE AND NODE_EXECUTABLE)
  set(UI_BUILD_DIR "${CMAKE_SOURCE_DIR}/mcp-apps/dashboard-ui")
  set(UI_OUTPUT_DIR "${UI_BUILD_DIR}/dist")
  
  # Collect all source files that should trigger a rebuild when changed
  file(GLOB_RECURSE UI_SOURCE_FILES
    "${UI_BUILD_DIR}/src/*"
    "${UI_BUILD_DIR}/public/*"
    "${UI_BUILD_DIR}/package.json"
    "${UI_BUILD_DIR}/package-lock.json"
    "${UI_BUILD_DIR}/vite.config.*"
    "${UI_BUILD_DIR}/tsconfig*.json"
    "${UI_BUILD_DIR}/build-ui.mjs"
    "${UI_BUILD_DIR}/index.html"
  )
  
  # Also include any TypeScript/JSX files
  file(GLOB_RECURSE UI_TYPE_FILES
    "${UI_BUILD_DIR}/src/*.ts"
    "${UI_BUILD_DIR}/src/*.tsx"
    "${UI_BUILD_DIR}/src/*.js"
    "${UI_BUILD_DIR}/src/*.jsx"
  )
  
  list(APPEND UI_SOURCE_FILES ${UI_TYPE_FILES})
  
  # Create a stamp file to track when dependencies were last installed
  set(NPM_INSTALL_STAMP "${CMAKE_CURRENT_BINARY_DIR}/npm_install.stamp")
  set(UI_BUILD_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ui_build.stamp")
  
  # Install npm dependencies if package.json has changed or stamp doesn't exist
  add_custom_command(
    OUTPUT ${NPM_INSTALL_STAMP}
    COMMAND ${NPM_EXECUTABLE} install
    COMMAND ${CMAKE_COMMAND} -E touch ${NPM_INSTALL_STAMP}
    WORKING_DIRECTORY ${UI_BUILD_DIR}
    DEPENDS ${UI_BUILD_DIR}/package.json ${UI_BUILD_DIR}/package-lock.json
    COMMENT "Installing npm dependencies for React UI..."
    VERBATIM
  )
  
  # Build the React app - this creates the dist/ directory with the built files
  # The build-ui.mjs script uses vite-plugin-singlefile to bundle everything into index.html
  add_custom_command(
    OUTPUT ${UI_BUILD_STAMP}
    COMMAND ${CMAKE_COMMAND} -E echo "Building React MCP App UI..."
    COMMAND ${NPM_EXECUTABLE} run build-ui
    COMMAND ${CMAKE_COMMAND} -E touch ${UI_BUILD_STAMP}
    WORKING_DIRECTORY ${UI_BUILD_DIR}
    DEPENDS ${UI_SOURCE_FILES} ${NPM_INSTALL_STAMP}
    COMMENT "Building MCP App Dashboard UI..."
    VERBATIM
  )
  
  # Create custom target that runs the build
  add_custom_target(build_mcp_ui ALL
    DEPENDS ${UI_BUILD_STAMP}
    COMMENT "MCP Dashboard UI build target"
  )
  
  # Optional: Add a target to watch for development (doesn't block build)
  add_custom_target(watch_mcp_ui
    COMMAND ${NPM_EXECUTABLE} run build-ui:watch
    WORKING_DIRECTORY ${UI_BUILD_DIR}
    COMMENT "Watching MCP Dashboard UI for changes..."
    VERBATIM
  )
  
  # Create a deployment directory and copy all built artifacts
  set(UI_DEPLOY_DIR "${CMAKE_BINARY_DIR}/ui_artifacts")
  
  # Copy all files from dist/ to deployment directory
  add_custom_command(
    OUTPUT ${UI_DEPLOY_DIR}/.deployed
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UI_DEPLOY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${UI_OUTPUT_DIR} ${UI_DEPLOY_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${UI_DEPLOY_DIR}/.deployed
    DEPENDS ${UI_BUILD_STAMP}
    COMMENT "Copying UI artifacts to deployment directory..."
    VERBATIM
  )
  
  add_custom_target(deploy_mcp_ui ALL
    DEPENDS ${UI_DEPLOY_DIR}/.deployed
    COMMENT "Deploy MCP UI artifacts"
  )
  
  # Make the MCP server depend on the UI build if it needs the UI at build time
  add_dependencies(fastmcp_server build_mcp_ui deploy_mcp_ui)
  
  # Optional: Generate a C++ header file with the embedded HTML/JS for serving directly
  # This runs after the UI is built and only if index.html exists
  if(CMAKE_CXX_COMPILER)
    set(UI_HEADER_FILE "${CMAKE_BINARY_DIR}/generated/mcp_ui_artifact.h")
    
    # Create a custom target that generates the header after the UI is built
    add_custom_target(embed_mcp_ui_in_cpp
      DEPENDS ${UI_BUILD_STAMP}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
      COMMAND ${CMAKE_COMMAND} -E echo "// Auto-generated MCP UI artifact" > ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "#ifndef MCP_UI_ARTIFACT_H" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "#define MCP_UI_ARTIFACT_H" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "// Index.html content" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "const char* MCP_UI_INDEX_HTML = R\"raw_content(" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E cat ${UI_OUTPUT_DIR}/index.html >> ${UI_HEADER_FILE} 2>/dev/null || ${CMAKE_COMMAND} -E echo "// index.html not found" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo ")raw_content\";" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "" >> ${UI_HEADER_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "#endif // MCP_UI_ARTIFACT_H" >> ${UI_HEADER_FILE}
      COMMENT "Embedding UI in C++ header (if index.html exists)..."
      VERBATIM
    )
    
    add_dependencies(fastmcp_server embed_mcp_ui_in_cpp)
  endif()
  
  # Print build output location for convenience
  add_custom_target(info_mcp_ui
    COMMAND ${CMAKE_COMMAND} -E echo "MCP UI build directory: ${UI_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "MCP UI deploy directory: ${UI_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "To watch for changes: make watch_mcp_ui"
    DEPENDS build_mcp_ui
    COMMENT "MCP UI build information"
  )
  
else()
  message(WARNING "npm or node not found - MCP Dashboard UI will not be built")
  message(WARNING "  npm found: ${NPM_EXECUTABLE}")
  message(WARNING "  node found: ${NODE_EXECUTABLE}")
endif()