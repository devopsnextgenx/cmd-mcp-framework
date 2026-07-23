#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
VENV_DIR="${VENV_DIR:-$PROJECT_DIR/.venv}"
PYTHON_BIN="${PYTHON_BIN:-python3.13}"
SETUP_ONLY=0

if [ "${1:-}" = "--setup-only" ]; then
  SETUP_ONLY=1
fi

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "Error: $PYTHON_BIN is not available. Ensure Python 3.13 is installed in the container." >&2
  exit 1
fi

if [ ! -d "$VENV_DIR" ]; then
  echo "Creating virtual environment at $VENV_DIR"
  "$PYTHON_BIN" -m venv "$VENV_DIR"
fi

if ! "$VENV_DIR/bin/python" -c "import mcp_python_server" >/dev/null 2>&1; then
  echo "Installing project dependencies"
  "$VENV_DIR/bin/python" -m pip install --upgrade pip
  "$VENV_DIR/bin/python" -m pip install -e "$PROJECT_DIR"
fi

cd "$PROJECT_DIR"

if [ "$SETUP_ONLY" -eq 1 ]; then
  echo "Python environment is ready in $VENV_DIR"
  exit 0
fi

exec "$VENV_DIR/bin/python" -m mcp_python_server.main