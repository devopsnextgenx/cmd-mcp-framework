#!/usr/bin/env -S just --justfile

default:
    @just --list --unsorted

build:
    ./build.sh

start:
    #!/usr/bin/env bash
    cd mcp-apps/dashboard-ui && npm run start &
    sleep 5
    export FASTMCP_MCP_DEBUG=1
    ./build/bin/fastmcp_server &
    sleep 5
    npx @modelcontextprotocol/inspector --url http://localhost:5432/mcp --transport http

start-docker:
    #!/usr/bin/env bash
    docker run --rm --name cmd-mcp-framework-test -p 5432:5432 -p 5433:5433 -p 6543:6543 -e FASTMCP_MCP_DEBUG=1 localhost/cmd-mcp-server:local &
    sleep 5
    npx @modelcontextprotocol/inspector --url http://localhost:5432/mcp --transport http
