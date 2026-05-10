add_executable(fastmcp_server
  src/main.cpp
  src/PluginLoader.cpp
)

target_link_libraries(fastmcp_server
  PRIVATE
    cmd_sdk
    nlohmann_json::nlohmann_json
)

if(TARGET httplib::httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib::httplib)
elseif(TARGET httplib)
  target_link_libraries(fastmcp_server PRIVATE httplib)
else()
  target_include_directories(fastmcp_server PRIVATE ${cpp_httplib_SOURCE_DIR})
endif()

target_compile_definitions(fastmcp_server
  PRIVATE
    CMDSDK_SERVER_PORT=5432
    CMDSDK_SAMPLE_PLUGIN_NAME="${CMAKE_SHARED_LIBRARY_PREFIX}math_cmd_provider${CMAKE_SHARED_LIBRARY_SUFFIX}"
)
