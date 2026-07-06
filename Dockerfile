# syntax=docker/dockerfile:1.7
#
# Build (BuildKit required — enabled by default with buildx):
#   docker buildx build -t docker.io/amitkshirsagar13/cmd-mcp-server:latest .
#   docker buildx build -t cmd-mcp-server:local --load .
#
# With registry-backed cache (recommended for ephemeral GitLab k8s runners):
#   docker buildx build \
#     --cache-to=type=registry,ref=docker.io/amitkshirsagar13/cmd-mcp-server:buildcache,mode=max \
#     --cache-from=type=registry,ref=docker.io/amitkshirsagar13/cmd-mcp-server:buildcache \
#     -t docker.io/amitkshirsagar13/cmd-mcp-server:latest --push .
#
# Run:
#   docker run --rm -d --name cmd-mcp-framework-test \
#     -p 5432:5432 -p 5433:5433 -p 6543:6543 \
#     -e FASTMCP_MCP_DEBUG=1 localhost/cmd-mcp-server:local

# =============================================================================
# BUILD STAGE
# =============================================================================
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
        ccache \
    && rm -rf /var/lib/apt/lists/*

ENV VCPKG_ROOT=/opt/vcpkg

RUN git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && sh "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY vcpkg.json vcpkg-configuration.jso[n] ./

# NOTE: no CC/CXX override here — vcpkg builds its ports with a clean,
# unwrapped compiler. This avoids breaking ports like boost-context whose
# portfiles invoke $CXX directly / parse the compiler string.
RUN --mount=type=cache,target=${VCPKG_ROOT}/downloads,sharing=locked \
    --mount=type=cache,target=${VCPKG_ROOT}/buildtrees,sharing=locked \
    --mount=type=cache,target=${VCPKG_ROOT}/packages,sharing=locked \
    "$VCPKG_ROOT/vcpkg" install --triplet x64-linux

COPY . /src

# ccache only wraps the compiler for OUR OWN source build, after vcpkg
# has already produced its .so/.a artifacts.
ENV CCACHE_DIR=/root/.cache/ccache
ENV PATH=/usr/lib/ccache:$PATH
ENV CC="ccache gcc"
ENV CXX="ccache g++"

RUN --mount=type=cache,target=/src/build/_deps,sharing=locked \
    --mount=type=cache,target=${CCACHE_DIR},sharing=locked \
    BUILD_MCP_UI=ON VCPKG_ROOT="$VCPKG_ROOT" sh /src/build.sh

RUN --mount=type=cache,target=/root/.npm,sharing=locked \
    npm --prefix /src/mcp-apps/dashboard-ui ci --omit=dev

# =============================================================================
# RUNTIME STAGE
# =============================================================================
FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libstdc++6 \
        libc6 \
        ca-certificates \
        curl \
        nodejs \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --uid 10001 appuser

WORKDIR /opt/cmd-mcp-server
RUN mkdir -p /opt/cmd-mcp-server/bin /opt/cmd-mcp-server/lib /opt/cmd-mcp-server/dashboard-ui

COPY --from=build /src/build/bin/fastmcp_server /opt/cmd-mcp-server/bin/fastmcp_server
COPY --from=build /src/build/lib/ /opt/cmd-mcp-server/lib/
COPY --from=build /src/mcp-apps/dashboard-ui/ /opt/cmd-mcp-server/dashboard-ui/
# node_modules already installed in the build stage — no npm required here.

COPY <<'EOF' /usr/local/bin/entrypoint.sh
#!/bin/sh
set -eu

# Default the UI server's listen port to the resource-server port the
# fastmcp_server client is configured to talk to, unless PORT is set
# explicitly (allows splitting listen-port from the advertised port).
UI_PORT="${PORT:-${MCP_APP_RESOURCE_SERVER_PORT:-6543}}"

/opt/cmd-mcp-server/bin/fastmcp_server "$@" &
mcp_pid=$!

(
  cd /opt/cmd-mcp-server/dashboard-ui
  PORT="$UI_PORT" ./node_modules/.bin/tsx server.ts
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

# mcp-apps resource server connection settings (overridable at runtime, e.g.
# `docker run -e MCP_APP_RESOURCE_SERVER_HOST=myhost -e MCP_APP_RESOURCE_SERVER_PORT=7000 ...`)
ENV MCP_APP_RESOURCE_SERVER_HOST=localhost
ENV MCP_APP_RESOURCE_SERVER_PORT=6543

EXPOSE 5432 5433 6543

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS "http://localhost:${MCP_APP_RESOURCE_SERVER_PORT}/health" || exit 1

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]