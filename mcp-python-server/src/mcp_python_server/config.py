from __future__ import annotations

import os
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
        default_bridge = project_root.parent / "build" / "bin" / "cmdsdk_bridge.dll"
        default_plugins = ",".join(
            [
                str(project_root.parent / "build" / "bin" / "greeting_cmd_provider.dll"),
                str(project_root.parent / "build" / "bin" / "math_cmd_provider.dll"),
            ]
        )

        return Settings(
            mcp_host=os.getenv("MCP_PYTHON_MCP_HOST", "0.0.0.0"),
            mcp_port=int(os.getenv("MCP_PYTHON_MCP_PORT", "5432")),
            mcp_path=os.getenv("MCP_PYTHON_MCP_PATH", "/mcp"),
            rest_host=os.getenv("MCP_PYTHON_REST_HOST", "0.0.0.0"),
            rest_port=int(os.getenv("MCP_PYTHON_REST_PORT", "5433")),
            bridge_dll_path=Path(os.getenv("MCP_PYTHON_BRIDGE_DLL", str(default_bridge))).resolve(),
            plugin_dlls=os.getenv("MCP_PYTHON_PLUGIN_DLLS", default_plugins),
            ui_dir=(project_root / "ui").resolve(),
        )
