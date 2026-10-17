#pragma once

#include <stddef.h>

#if defined(_WIN32)
#define CMDSDK_BRIDGE_API __declspec(dllexport)
#else
#define CMDSDK_BRIDGE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

CMDSDK_BRIDGE_API int cmdsdk_bridge_init();
CMDSDK_BRIDGE_API int cmdsdk_bridge_shutdown();

CMDSDK_BRIDGE_API int cmdsdk_bridge_load_plugin(const char* plugin_path);
CMDSDK_BRIDGE_API int cmdsdk_bridge_load_plugins_csv(const char* plugin_csv);

CMDSDK_BRIDGE_API const char* cmdsdk_bridge_list_commands_json();
CMDSDK_BRIDGE_API const char* cmdsdk_bridge_execute_json(const char* session_id,
                                                         const char* command_name,
                                                         const char* args_json);
CMDSDK_BRIDGE_API const char* cmdsdk_bridge_get_session_state_json(const char* session_id);
CMDSDK_BRIDGE_API void cmdsdk_bridge_reset_session(const char* session_id);

CMDSDK_BRIDGE_API const char* cmdsdk_bridge_last_error();
CMDSDK_BRIDGE_API void cmdsdk_bridge_free_string(const char* value);

#ifdef __cplusplus
}
#endif
