from __future__ import annotations

import copy
import inspect
from urllib.parse import urlparse
from typing import Annotated, Any, Literal

from pydantic import Field

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations

from .bridge_client import BridgeClient, ExternalResourceInfo


def create_mcp_server(bridge: BridgeClient) -> FastMCP:
    mcp = FastMCP(name="cmdsdk-python-bridge")

    @mcp.tool(name="bridge.session_state", description="Get current session state from bridge")
    def bridge_session_state(session_id: str = "default") -> dict[str, Any]:
        return bridge.get_session(session_id)

    commands_payload = bridge.list_commands()
    commands: list[dict[str, Any]] = commands_payload.get("commands", [])

    # Track resources/tools to avoid duplicate registration
    seen_resource_uris: set[str] = set()
    seen_tool_names: set[str] = {"bridge.session_state"}
    seen_app_tool_names: set[str] = set()

    # Register command-based tools and resources
    for command in commands:
        # Only register as tool if is_tool is true
        is_tool = bool(command.get("is_tool", False))
        if is_tool:
            register_bridge_tool(mcp, bridge, command, seen_tool_names)
        
        # Register resources for app tools
        is_app_tool = bool(command.get("is_app_tool", False))
        cmd_name = str(command.get("cmd_name", "")).strip()
        resource_uri = command.get("resource_uri", "").strip()
        app_tool_name = f"open_{cmd_name.replace('.', '_')}_form" if cmd_name else ""
        
        if (
            is_app_tool
            and cmd_name
            and app_tool_name
            and app_tool_name not in seen_app_tool_names
            and resource_uri
            and resource_uri not in seen_resource_uris
        ):
            description = command.get("description", "Bridge command UI")
            input_schema = _build_input_schema(command)
            register_app_tool_resource(mcp, bridge, resource_uri, cmd_name, description, input_schema)
            seen_resource_uris.add(resource_uri)
            seen_app_tool_names.add(app_tool_name)
    
    # Fetch and register external resources from the resource manifest
    external_resources = bridge.fetch_external_resources()
    for ext_resource in external_resources:
        if ext_resource.uri not in seen_resource_uris:
            register_external_resource(mcp, bridge, ext_resource)
            seen_resource_uris.add(ext_resource.uri)

    @mcp.resource("docs://overview")
    def docs_overview() -> str:
        return (
            "This MCP server is hosted by Python and backed by cmdsdk_bridge. "
            "Plugin commands are loaded from DLLs and registered as MCP tools. "
            "UI-enabled commands are exposed as app tools with card-based UIs. "
            "External resources from mcp-apps are also served as resources."
        )

    return mcp


def register_bridge_tool(
    mcp: FastMCP,
    bridge: BridgeClient,
    command_meta: dict[str, Any],
    seen_tool_names: set[str] | None = None,
) -> None:
    command_name = command_meta.get("cmd_name", "")
    description = command_meta.get("description", "Bridge command")
    if not command_name:
        return

    input_schema = _build_input_schema(command_meta)

    def tool_impl(**kwargs: Any) -> dict[str, Any]:
        return bridge.execute(session_id="default", command_name=command_name, arguments=kwargs)

    tool_impl.__name__ = f"{command_name.replace('.', '_')}_tool"
    tool_impl.__signature__ = _build_signature_from_schema(input_schema)

    # Skip duplicate tool names from repeated command metadata entries.
    if seen_tool_names is not None and command_name in seen_tool_names:
        return

    existing_tool = mcp._tool_manager.get_tool(command_name) if hasattr(mcp, "_tool_manager") else None
    if existing_tool is not None:
        if seen_tool_names is not None:
            seen_tool_names.add(command_name)
        return

    # Register one MCP tool per command metadata entry.
    mcp.tool(name=command_name, title=command_name, description=description)(tool_impl)
    if seen_tool_names is not None:
        seen_tool_names.add(command_name)

    # Keep list_tools aligned with command metadata schema instead of Pydantic's expanded form.
    registered_tool = mcp._tool_manager.get_tool(command_name)
    if registered_tool is not None:
        registered_tool.parameters = copy.deepcopy(input_schema)


def register_app_tool_resource(
    mcp: FastMCP, 
    bridge: BridgeClient, 
    resource_uri: str, 
    cmd_name: str,
    description: str,
    input_schema: dict[str, Any]
) -> None:
    """
    Register a resource for an app tool (card-based UI).
    
    Mirrors the C++ implementation via registerAppToolWithUI:
    - Creates tool with "open_<command>_form" naming
    - Attaches UI resource metadata
    - Sets read-only and forbidden-task annotations
    """
    # Generate tool name matching C++ convention
    tool_base_name = f"open_{cmd_name.replace('.', '_')}_form"
    title = f"MCP app ui for {cmd_name}"
    tool_description = f"MCP app tool ui to collect and invoke MCP tool command {cmd_name}."
    
    def ui_handler(args: dict[str, Any]) -> dict[str, Any]:
        """Handler that returns UI availability and metadata."""
        try:
            # Fetch the resource URL to verify it's available
            resource_content = _read_mcp_app_resource(bridge, resource_uri)
            
            response = {
                "status": "success",
                "availability": f"{cmd_name} form available",
                "message": "UI form available",
                "resourceUri": resource_uri,
                "commandName": cmd_name,
            }
            if args and not isinstance(args, type(None)):
                response["args"] = args
            
            return response
        except Exception as e:
            return {
                "status": "error",
                "error": str(e),
                "message": f"Failed to load UI form for {cmd_name}"
            }
    
    # Register with UI metadata
    register_app_tool_with_ui(
        mcp=mcp,
        bridge=bridge,
        tool_name=tool_base_name,
        title=title,
        description=tool_description,
        resource_uri=resource_uri,
        input_schema=input_schema,
        handler=ui_handler
    )


def register_app_tool_with_ui(
    mcp: FastMCP,
    bridge: BridgeClient,
    tool_name: str,
    title: str,
    description: str,
    resource_uri: str,
    input_schema: dict[str, Any],
    handler: Any
) -> None:
    """
    Register a tool with UI metadata, mirroring C++ registerToolWithUI.
    
    Sets up:
    - metadata.ui.resourceUri: Points to the UI resource
    - annotations.readOnlyHint: true (read-only form)
    - execution.taskSupport: forbidden (no background tasks)
    """
    # Create wrapper handler that returns CallToolResult-like structure
    def tool_handler(**kwargs: Any) -> dict[str, Any]:
        """Handle tool call and return result with metadata."""
        try:
            result = handler(kwargs)
            return result
        except Exception as e:
            return {
                "status": "error",
                "error": str(e),
                "toolName": tool_name
            }
    
    # Register tool with FastMCP.
    tool_annotations = ToolAnnotations(readOnlyHint=True)
    tool_meta = {
        "ui": {"resourceUri": resource_uri},
        "ui/resourceUri": resource_uri,
        "execution": {"taskSupport": "forbidden"},
    }

    mcp.tool(
        name=tool_name,
        title=title,
        description=description,
        annotations=tool_annotations,
        meta=copy.deepcopy(tool_meta),
    )(tool_handler)
    
    # Try to attach UI metadata to the registered tool
    registered_tool = mcp._tool_manager.get_tool(tool_name) if hasattr(mcp, '_tool_manager') else None
    
    if registered_tool is not None:
        # Set input schema (already done by FastMCP, but ensure it's correct)
        registered_tool.parameters = copy.deepcopy(input_schema)

        existing_meta = registered_tool.meta if isinstance(registered_tool.meta, dict) else {}
        merged_meta = copy.deepcopy(existing_meta)
        merged_meta.update(tool_meta)
        registered_tool.meta = merged_meta

        registered_tool.annotations = ToolAnnotations(
            **{
                **(
                    registered_tool.annotations.model_dump(exclude_none=True)
                    if registered_tool.annotations is not None
                    else {}
                ),
                "readOnlyHint": True,
            }
        )


def register_external_resource(
    mcp: FastMCP, 
    bridge: BridgeClient, 
    ext_resource: ExternalResourceInfo
) -> None:
    """
    Register an external resource from the resource manifest.
    
    External resources have metadata including the actual URL to fetch from,
    enabling proper content delivery with correct mime types.
    """
    def resource_content_provider() -> str:
        """Fetch the external resource content from the URL in manifest."""
        try:
            if ext_resource.resource_url:
                # Use the direct URL from the resource manifest
                content, _ = bridge.fetch_resource_by_url(ext_resource.resource_url)
                if content:
                    return content
            
            # Fallback: try URI-based fetch
            content = _read_mcp_app_resource(bridge, ext_resource.uri)
            return content if content else f"<p>No content available for {ext_resource.uri}</p>"
        except Exception as e:
            return f"<p>Error loading resource: {str(e)}</p>"
    
    # Register the resource with FastMCP using the manifest metadata
    mcp.resource(ext_resource.uri, name=ext_resource.name, description=ext_resource.description)(
        resource_content_provider
    )


def _read_mcp_app_resource(bridge: BridgeClient, resource_uri: str) -> str:
    """
    Read MCP app resource content via the bridge.
    
    Attempts to retrieve HTML or other resource content from the mcp-apps server
    for serving UI to chat clients. Mirrors fastmcp::readMcpAppResource from C++.
    """
    try:
        if not resource_uri:
            return ""

        base_url = "http://localhost:6543"
        candidates: list[str] = []

        print(f"Attempting to read MCP app resource for URI: {resource_uri}")

        parsed = urlparse(resource_uri)
        if parsed.scheme in {"http", "https"}:
            candidates.append(resource_uri)
        elif resource_uri.startswith("ui://"):
            # Convert ui://ui/math-form(.html) -> http://localhost:6543/ui/math-form(.html)
            path = "/" + resource_uri[5:].lstrip("/")
            candidates.append(f"{base_url}{path}")

            # If no extension is provided in URI, try .html fallback.
            leaf = path.rsplit("/", 1)[-1]
            if "." not in leaf:
                candidates.append(f"{base_url}{path}.html")
        else:
            path = resource_uri if resource_uri.startswith("/") else f"/{resource_uri}"
            candidates.append(f"{base_url}{path}")

        seen: set[str] = set()
        for candidate in candidates:
            if candidate in seen:
                continue
            seen.add(candidate)
            print(f"Trying to fetch resource from: {candidate}")
            content, _ = bridge.fetch_resource_by_url(candidate)
            if content:
                return content

        # Fallback to the bridge's URI-based resolver.
        content = bridge.read_resource(resource_uri)
        return content if content else ""
    except Exception:
        return ""


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
