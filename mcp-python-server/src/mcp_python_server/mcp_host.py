from __future__ import annotations

import copy
import inspect
from typing import Annotated, Any, Literal

from pydantic import Field

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

    input_schema = _build_input_schema(command_meta)

    def tool_impl(**kwargs: Any) -> dict[str, Any]:
        return bridge.execute(session_id="default", command_name=command_name, arguments=kwargs)

    tool_impl.__name__ = f"{command_name.replace('.', '_')}_tool"
    tool_impl.__signature__ = _build_signature_from_schema(input_schema)

    # Register one MCP tool per command metadata entry.
    mcp.tool(name=command_name, title=command_name, description=description)(tool_impl)

    # Keep list_tools aligned with command metadata schema instead of Pydantic's expanded form.
    registered_tool = mcp._tool_manager.get_tool(command_name)
    if registered_tool is not None:
        registered_tool.parameters = copy.deepcopy(input_schema)


def _build_input_schema(command_meta: dict[str, Any]) -> dict[str, Any]:
    # If bridge metadata already includes a schema, prefer it.
    existing_schema = command_meta.get("inputSchema")
    if isinstance(existing_schema, dict):
        return existing_schema

    input_schema: dict[str, Any] = {
        "type": "object",
        "properties": {},
        "additionalProperties": True,
    }

    required_fields: list[str] = []
    properties: dict[str, Any] = input_schema["properties"]

    for param in command_meta.get("parameters", []):
        if not isinstance(param, dict):
            continue

        name = str(param.get("parameter_name", "")).strip()
        if not name:
            continue

        properties[name] = {
            "type": _normalize_json_schema_type(str(param.get("parameter_type", "string"))),
            "description": str(param.get("description", "")),
        }
        if bool(param.get("required", False)):
            required_fields.append(name)

    sub_cmd_types = command_meta.get("sub_cmd_types", [])
    if isinstance(sub_cmd_types, list) and sub_cmd_types:
        enum_values = [
            str(item.get("sub_type_name", "")).strip()
            for item in sub_cmd_types
            if isinstance(item, dict) and str(item.get("sub_type_name", "")).strip()
        ]
        if enum_values:
            properties["subType"] = {
                "type": "string",
                "enum": enum_values,
                "description": "SubCommand type to execute",
            }
            required_fields.append("subType")

    if required_fields:
        # Preserve declaration order while removing duplicates.
        input_schema["required"] = list(dict.fromkeys(required_fields))

    return input_schema


def _normalize_json_schema_type(type_name: str) -> str:
    if type_name in {"number", "boolean", "object", "array", "integer", "string"}:
        return type_name
    return "string"


def _build_signature_from_schema(schema: dict[str, Any]) -> inspect.Signature:
    properties = schema.get("properties", {})
    required = set(schema.get("required", []))
    if not isinstance(properties, dict):
        properties = {}

    parameters: list[inspect.Parameter] = []
    for name, prop_schema in properties.items():
        if not isinstance(name, str) or not name:
            continue

        field_schema = prop_schema if isinstance(prop_schema, dict) else {}
        annotation = _annotation_from_property_schema(field_schema)

        default = inspect.Parameter.empty if name in required else None
        parameters.append(
            inspect.Parameter(
                name=name,
                kind=inspect.Parameter.KEYWORD_ONLY,
                default=default,
                annotation=annotation,
            )
        )

    return inspect.Signature(parameters=parameters, return_annotation=dict[str, Any])


def _annotation_from_property_schema(prop_schema: dict[str, Any]) -> Any:
    schema_type = str(prop_schema.get("type", "string"))
    py_type: Any
    if schema_type == "number":
        py_type = float
    elif schema_type == "integer":
        py_type = int
    elif schema_type == "boolean":
        py_type = bool
    elif schema_type == "object":
        py_type = dict[str, Any]
    elif schema_type == "array":
        py_type = list[Any]
    else:
        py_type = str

    enum_values = prop_schema.get("enum")
    if isinstance(enum_values, list) and enum_values:
        enum_literals = tuple(str(value) for value in enum_values)
        py_type = Literal.__getitem__(enum_literals)

    description = prop_schema.get("description")
    if isinstance(description, str) and description.strip():
        return Annotated[py_type, Field(description=description.strip())]
    return py_type
