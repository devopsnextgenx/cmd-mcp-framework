from __future__ import annotations

import ctypes
import json
from pathlib import Path
from typing import Any
import urllib.request
import urllib.error


class BridgeError(RuntimeError):
    pass


class ExternalResourceInfo:
    """Represents a resource from the external resource manifest."""
    def __init__(self, uri: str, name: str, description: str, mime_type: str, resource_url: str = ""):
        self.uri = uri
        self.name = name
        self.description = description
        self.mime_type = mime_type
        self.resource_url = resource_url  # Direct URL to fetch the resource


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
        - ui://ui/math-form → /ui/math-form.html (via resource manifest)
        """
        try:
            # Convert ui:// URIs to HTTP paths
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

    def fetch_external_resources(self) -> list[ExternalResourceInfo]:
        """
        Fetch the resource manifest from the mcp-apps server and extract resources.
        
        Mirrors C++ fetchExternalAppResources: reads /resource-manifest.json
        and extracts all available external resources with their metadata.
        """
        resources: list[ExternalResourceInfo] = []
        
        try:
            # Fetch resource manifest from mcp-apps server
            url = "http://localhost:6543/resource-manifest.json"
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    manifest_data = json.loads(response.read().decode("utf-8"))
                    
                    if not isinstance(manifest_data, dict) or "resources" not in manifest_data:
                        return resources
                    
                    for resource_entry in manifest_data["resources"]:
                        if not isinstance(resource_entry, dict) or "uri" not in resource_entry:
                            continue
                        
                        uri = resource_entry.get("uri", "").strip()
                        if not uri:
                            continue
                        
                        name = resource_entry.get("name", f"App Resource: {uri}")
                        description = resource_entry.get("description", "Resource from local mcp-apps")
                        mime_type = resource_entry.get("mimeType", "application/json")
                        
                        # Extract the actual resource URL from _meta if available
                        resource_url = ""
                        meta = resource_entry.get("_meta", {})
                        if isinstance(meta, dict):
                            ui_meta = meta.get("ui", {})
                            if isinstance(ui_meta, dict):
                                resource_url = ui_meta.get("resourceUri", "")
                        
                        resources.append(ExternalResourceInfo(
                            uri=uri,
                            name=name,
                            description=description,
                            mime_type=mime_type,
                            resource_url=resource_url
                        ))
        except (urllib.error.URLError, urllib.error.HTTPError, OSError, TimeoutError, json.JSONDecodeError):
            pass
        
        return resources

    def fetch_resource_by_url(self, url: str) -> tuple[str, str]:
        """
        Fetch resource content from a direct URL.
        
        Returns tuple of (content, mime_type). If fetch fails, returns ("", "").
        """
        try:
            with urllib.request.urlopen(url, timeout=2) as response:
                if response.status == 200:
                    content = response.read().decode("utf-8")
                    # Try to get mime type from response headers
                    content_type = response.headers.get("content-type", "application/json")
                    # Extract mime type before semicolon if present
                    mime_type = content_type.split(";")[0].strip()
                    return content, mime_type
        except (urllib.error.URLError, urllib.error.HTTPError, OSError, TimeoutError):
            pass
        
        return "", ""

    def _uri_to_path(self, uri: str) -> str:
        """
        Convert ui:// URI to HTTP path.
        
        Examples:
        - ui://ui/math-form → /ui/math-form
        - ui://ui/geo-form.html → /ui/geo-form.html
        """
        if not uri or not isinstance(uri, str):
            return ""
        
        # Handle ui:// scheme
        if uri.startswith("ui://"):
            rest = uri[5:]  # Remove "ui://" prefix
            # If it starts with "ui/", convert to path format
            if rest.startswith("ui/"):
                return "/" + rest
            return "/" + rest
        
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
