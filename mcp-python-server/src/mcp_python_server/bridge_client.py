from __future__ import annotations

import ctypes
import json
from pathlib import Path
from typing import Any
import urllib.request
import urllib.error


class BridgeError(RuntimeError):
    pass


class BridgeClient:
    def __init__(self, dll_path: Path) -> None:
        if not dll_path.exists():
            raise BridgeError(f"Bridge DLL not found: {dll_path}")

        self._dll = ctypes.CDLL(str(dll_path))
        self._configure_signatures()

    def _configure_signatures(self) -> None:
        self._dll.cmdsdk_bridge_init.argtypes = []
        self._dll.cmdsdk_bridge_init.restype = ctypes.c_int

        self._dll.cmdsdk_bridge_shutdown.argtypes = []
        self._dll.cmdsdk_bridge_shutdown.restype = ctypes.c_int

        self._dll.cmdsdk_bridge_load_plugin.argtypes = [ctypes.c_char_p]
        self._dll.cmdsdk_bridge_load_plugin.restype = ctypes.c_int

        self._dll.cmdsdk_bridge_load_plugins_csv.argtypes = [ctypes.c_char_p]
        self._dll.cmdsdk_bridge_load_plugins_csv.restype = ctypes.c_int

        self._dll.cmdsdk_bridge_list_commands_json.argtypes = []
        self._dll.cmdsdk_bridge_list_commands_json.restype = ctypes.c_void_p

        self._dll.cmdsdk_bridge_execute_json.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_char_p,
        ]
        self._dll.cmdsdk_bridge_execute_json.restype = ctypes.c_void_p

        self._dll.cmdsdk_bridge_get_session_state_json.argtypes = [ctypes.c_char_p]
        self._dll.cmdsdk_bridge_get_session_state_json.restype = ctypes.c_void_p

        self._dll.cmdsdk_bridge_reset_session.argtypes = [ctypes.c_char_p]
        self._dll.cmdsdk_bridge_reset_session.restype = None

        self._dll.cmdsdk_bridge_last_error.argtypes = []
        self._dll.cmdsdk_bridge_last_error.restype = ctypes.c_void_p

        self._dll.cmdsdk_bridge_free_string.argtypes = [ctypes.c_void_p]
        self._dll.cmdsdk_bridge_free_string.restype = None

    def initialize(self) -> None:
        status = self._dll.cmdsdk_bridge_init()
        if status != 0:
            raise BridgeError(self.last_error())

    def shutdown(self) -> None:
        self._dll.cmdsdk_bridge_shutdown()

    def load_plugins_csv(self, plugin_csv: str) -> None:
        status = self._dll.cmdsdk_bridge_load_plugins_csv(plugin_csv.encode("utf-8"))
        if status != 0:
            raise BridgeError(self.last_error())

    def list_commands(self) -> dict[str, Any]:
        payload = self._read_allocated_string(self._dll.cmdsdk_bridge_list_commands_json())
        return json.loads(payload or "{}")

    def execute(self, session_id: str, command_name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        payload = self._read_allocated_string(
            self._dll.cmdsdk_bridge_execute_json(
                session_id.encode("utf-8"),
                command_name.encode("utf-8"),
                json.dumps(arguments).encode("utf-8"),
            )
        )

        if not payload or payload == "{}":
            raise BridgeError(self.last_error())

        return json.loads(payload)

    def get_session(self, session_id: str) -> dict[str, Any]:
        payload = self._read_allocated_string(
            self._dll.cmdsdk_bridge_get_session_state_json(session_id.encode("utf-8"))
        )
        return json.loads(payload or "{}")

    def reset_session(self, session_id: str) -> None:
        self._dll.cmdsdk_bridge_reset_session(session_id.encode("utf-8"))

    def last_error(self) -> str:
        return self._read_allocated_string(self._dll.cmdsdk_bridge_last_error())

    def read_resource(self, resource_uri: str) -> str:
        """
        Read MCP app resource content via HTTP from the mcp-apps server.
        
        Mirrors C++ readMcpAppResource: converts resource URI to HTTP path
        and fetches content from localhost:6543 (default mcp-apps port).
        
        Supports URI schemes like:
        - mcp-app://approval-panel/approval-panel.html → /approval-panel.html
        """
        try:
            # Convert mcp-app:// URIs to HTTP paths
            path = self._uri_to_path(resource_uri)
            if not path:
                return ""
            
            # Ensure path starts with /
            if not path.startswith("/"):
                path = "/" + path
            
            # Fetch from mcp-apps server (default: localhost:6543)
            mcp_apps_host = "localhost"
            mcp_apps_port = 6543
            
            url = f"http://{mcp_apps_host}:{mcp_apps_port}{path}"
            
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    content = response.read().decode("utf-8")
                    return content
        except (urllib.error.URLError, urllib.error.HTTPError, OSError, TimeoutError):
            pass
        
        return ""

    def _uri_to_path(self, uri: str) -> str:
        """
        Convert mcp-app:// URI to HTTP path.
        
        Examples:
        - mcp-app://approval-panel/approval-panel.html → /approval-panel/approval-panel.html
        - mcp-app://dashboard-ui/index.html → /dashboard-ui/index.html
        """
        if not uri or not isinstance(uri, str):
            return ""
        
        # Handle mcp-app:// scheme
        if uri.startswith("mcp-app://"):
            return uri[9:]  # Remove "mcp-app://" prefix and keep the rest
        
        # For other schemes or plain paths, return as-is
        return uri

    def _read_allocated_string(self, pointer: int | None) -> str:
        if not pointer:
            return ""
        try:
            value = ctypes.string_at(pointer).decode("utf-8")
            return value
        finally:
            self._dll.cmdsdk_bridge_free_string(pointer)
