from __future__ import annotations

import asyncio
from contextlib import suppress

import uvicorn

from .bridge_client import BridgeClient
from .config import Settings
from .mcp_host import create_mcp_server
from .rest_host import create_rest_app


async def serve_uvicorn(app: object, host: str, port: int) -> None:
    config = uvicorn.Config(app=app, host=host, port=port, log_level="info")
    server = uvicorn.Server(config)
    await server.serve()


async def run_servers() -> None:
    settings = Settings.from_env()

    bridge = BridgeClient(settings.bridge_dll_path)
    bridge.initialize()
    bridge.load_plugins_csv(settings.plugin_dlls)

    mcp_server = create_mcp_server(bridge)
    mcp_app = mcp_server.streamable_http_app()

    rest_app = create_rest_app(settings, bridge)

    mcp_task = asyncio.create_task(serve_uvicorn(mcp_app, settings.mcp_host, settings.mcp_port))
    rest_task = asyncio.create_task(serve_uvicorn(rest_app, settings.rest_host, settings.rest_port))

    try:
        await asyncio.gather(mcp_task, rest_task)
    finally:
        for task in (mcp_task, rest_task):
            task.cancel()
            with suppress(asyncio.CancelledError):
                await task
        bridge.shutdown()


def run() -> None:
    asyncio.run(run_servers())


if __name__ == "__main__":
    run()
