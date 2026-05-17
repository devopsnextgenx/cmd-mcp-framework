#!/usr/bin/env -S just --justfile

default:
    @just --list --unsorted

build:
    ./build.sh

start:
    #!/usr/bin/env bash
    cd mcp-apps/dashboard-ui && npm run start &
    sleep 5
    ./build/bin/fastmcp_server &
    sleep 5
    npx @modelcontextprotocol/inspector --url http://localhost:5432/mcp --transport http