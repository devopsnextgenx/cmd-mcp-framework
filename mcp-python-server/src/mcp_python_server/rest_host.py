from __future__ import annotations

import json
from typing import Any

import httpx
from fastapi import Body, FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from .bridge_client import BridgeClient, BridgeError
from .config import Settings


def create_rest_app(settings: Settings, bridge: BridgeClient) -> FastAPI:
    app = FastAPI(
        title="cmdsdk Python REST Host",
        version="0.1.0",
        description="REST facade for cmdsdk bridge plus MCP dashboard",
    )

    static_dir = settings.ui_dir / "static"
    app.mount("/static", StaticFiles(directory=str(static_dir)), name="static")

    @app.get("/", include_in_schema=False)
    async def dashboard() -> FileResponse:
        return FileResponse(str(settings.ui_dir / "index.html"))

    @app.get("/health")
    async def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/api/commands")
    async def list_commands() -> dict[str, Any]:
        return bridge.list_commands()

    @app.post("/api/commands/{command_name}")
    async def execute_command(command_name: str, payload: dict[str, Any] = Body(default_factory=dict)) -> dict[str, Any]:
        session_id = str(payload.get("session_id", "default"))
        arguments = payload.get("arguments", payload)
        if not isinstance(arguments, dict):
            raise HTTPException(status_code=400, detail="arguments must be a JSON object")

        try:
            return bridge.execute(session_id=session_id, command_name=command_name, arguments=arguments)
        except BridgeError as ex:
            raise HTTPException(status_code=400, detail=str(ex)) from ex

    @app.get("/api/sessions/{session_id}")
    async def get_session(session_id: str) -> dict[str, Any]:
        return bridge.get_session(session_id)

    @app.delete("/api/sessions/{session_id}")
    async def clear_session(session_id: str) -> dict[str, str]:
        bridge.reset_session(session_id)
        return {"status": "cleared", "session_id": session_id}

    @app.post("/api/mcp-proxy")
    async def proxy_mcp(body: dict[str, Any] = Body(default_factory=dict)) -> Any:
        headers = {"Content-Type": "application/json"}
        async with httpx.AsyncClient(timeout=20) as client:
            response = await client.post(settings.mcp_url, json=body, headers=headers)

        if response.status_code >= 400:
            raise HTTPException(status_code=response.status_code, detail=response.text)

        try:
            return response.json()
        except json.JSONDecodeError:
            return {"status": response.status_code, "text": response.text}

    return app
