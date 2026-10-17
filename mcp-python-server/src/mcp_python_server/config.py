from __future__ import annotations

import os
import platform
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Settings:
    mcp_host: str
    mcp_port: int
    mcp_path: str
    rest_host: str
    rest_port: int
    bridge_dll_path: Path
    plugin_dlls: str
    ui_dir: Path

    @property
    def mcp_url(self) -> str:
        return f"http://{self.mcp_host}:{self.mcp_port}{self.mcp_path}"

    @staticmethod
    def from_env() -> "Settings":
        project_root = Path(__file__).resolve().parents[2]

        system = platform.system().lower()
        if system == "windows":
            lib_prefix = ""
            lib_suffix = ".dll"
        elif system == "darwin":
            lib_prefix = "lib"
            lib_suffix = ".dylib"
        else:
            lib_prefix = "lib"
            lib_suffix = ".so"

        # CMake emits shared libs to build/lib on Linux/macOS and to build/bin on Windows.
        default_lib_dir = project_root.parent / "build" / "lib"
        if not default_lib_dir.exists():
            default_lib_dir = project_root.parent / "build" / "bin"

        default_bridge = default_lib_dir / f"{lib_prefix}cmdsdk_bridge{lib_suffix}"
        default_plugins = ",".join(
            [
                str(default_lib_dir / f"{lib_prefix}greeting_cmd_provider{lib_suffix}"),
                str(default_lib_dir / f"{lib_prefix}math_cmd_provider{lib_suffix}"),
            ]
        )

        bridge_env = os.getenv("MCP_PYTHON_BRIDGE_LIB") or os.getenv("MCP_PYTHON_BRIDGE_DLL")
        plugins_env = os.getenv("MCP_PYTHON_PLUGIN_LIBS") or os.getenv("MCP_PYTHON_PLUGIN_DLLS")

        return Settings(
            mcp_host=os.getenv("MCP_PYTHON_MCP_HOST", "0.0.0.0"),
            mcp_port=int(os.getenv("MCP_PYTHON_MCP_PORT", "5432")),
            mcp_path=os.getenv("MCP_PYTHON_MCP_PATH", "/mcp"),
            rest_host=os.getenv("MCP_PYTHON_REST_HOST", "0.0.0.0"),
            rest_port=int(os.getenv("MCP_PYTHON_REST_PORT", "5433")),
            bridge_dll_path=Path(bridge_env or str(default_bridge)).resolve(),
            plugin_dlls=plugins_env or default_plugins,
            ui_dir=(project_root / "ui").resolve(),
        )
