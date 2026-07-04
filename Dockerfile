FROM debian:bookworm-slim AS build

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        make \
        pkg-config \
        patch \
        curl \
        ca-certificates \
        unzip \
        zip \
        tar \
        nodejs \
        npm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && sh "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

RUN BUILD_MCP_UI=ON VCPKG_ROOT="$VCPKG_ROOT" sh /src/build.sh

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libstdc++6 \
        libc6 \
        ca-certificates \
        nodejs \
        npm \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --uid 10001 appuser

WORKDIR /opt/cmd-mcp-server
RUN mkdir -p /opt/cmd-mcp-server/bin /opt/cmd-mcp-server/lib /opt/cmd-mcp-server/dashboard-ui

COPY --from=build /src/build/bin/fastmcp_server /opt/cmd-mcp-server/bin/fastmcp_server
COPY --from=build /src/build/lib/ /opt/cmd-mcp-server/lib/
COPY --from=build /src/mcp-apps/dashboard-ui/ /opt/cmd-mcp-server/dashboard-ui/

RUN npm --prefix /opt/cmd-mcp-server/dashboard-ui ci

COPY <<'EOF' /usr/local/bin/entrypoint.sh
#!/bin/sh
set -eu

/opt/cmd-mcp-server/bin/fastmcp_server "$@" &
mcp_pid=$!

(
  cd /opt/cmd-mcp-server/dashboard-ui
  PORT="${PORT:-6543}" ./node_modules/.bin/tsx server.ts
) &
ui_pid=$!

cleanup() {
  kill "$mcp_pid" "$ui_pid" 2>/dev/null || true
  wait "$mcp_pid" "$ui_pid" 2>/dev/null || true
}

trap cleanup INT TERM EXIT
wait "$mcp_pid"
status=$?
kill "$ui_pid" 2>/dev/null || true
wait "$ui_pid" 2>/dev/null || true
exit "$status"
EOF

RUN chmod +x /usr/local/bin/entrypoint.sh \
    && chown -R appuser:appuser /opt/cmd-mcp-server

USER appuser
ENV FASTMCP_MCP_DEBUG=1
ENV PATH=/opt/cmd-mcp-server/bin:$PATH
ENV LD_LIBRARY_PATH=/opt/cmd-mcp-server/lib:${LD_LIBRARY_PATH}
ENV NODE_ENV=production
EXPOSE 5432 5433 6543
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
