from __future__ import annotations

from typing import Any

from mcp.server.fastmcp import FastMCP

from .bridge_client import BridgeClient


def create_mcp_server(bridge: BridgeClient) -> FastMCP:
    mcp = FastMCP(name="cmdsdk-python-bridge")

    @mcp.tool(name="bridge.session_state", description="Get current session state from bridge")
    def bridge_session_state(session_id: str = "default") -> dict[str, Any]:
        return bridge.get_session(session_id)

    commands_payload = bridge.list_commands()
    commands: list[dict[str, Any]] = commands_payload.get("commands", [])

    for command in commands:
        register_bridge_tool(mcp, bridge, command)

    @mcp.resource("docs://overview")
    def docs_overview() -> str:
        return (
            "This MCP server is hosted by Python and backed by cmdsdk_bridge. "
            "Plugin commands are loaded from DLLs and registered as MCP tools."
        )

    return mcp


def register_bridge_tool(mcp: FastMCP, bridge: BridgeClient, command_meta: dict[str, Any]) -> None:
    command_name = command_meta.get("cmd_name", "")
    description = command_meta.get("description", "Bridge command")
    if not command_name:
        return

    def tool_impl(session_id: str = "default", arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        payload = arguments or {}
        return bridge.execute(session_id=session_id, command_name=command_name, arguments=payload)

    # Register one MCP tool per command metadata entry.
    mcp.tool(name=command_name, description=description)(tool_impl)
