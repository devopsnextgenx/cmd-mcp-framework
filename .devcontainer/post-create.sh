#!/usr/bin/env bash
set -euo pipefail

# Install UI dependencies so CMake custom UI build target can run.
if [ -f "mcp-apps/dashboard-ui/package-lock.json" ]; then
  npm --prefix mcp-apps/dashboard-ui ci
elif [ -f "mcp-apps/dashboard-ui/package.json" ]; then
  npm --prefix mcp-apps/dashboard-ui install
fi
