#!/bin/sh

set -eu

VCPKG_ROOT="${VCPKG_ROOT:-/usr/local/vcpkg}"
VCPKG_REPO_URL="${VCPKG_REPO_URL:-https://github.com/microsoft/vcpkg.git}"
BUILD_DIR="${BUILD_DIR:-build}"
VCPKG_MANIFEST_DIR="${VCPKG_MANIFEST_DIR:-$PWD}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
VCPKG_UPDATE="${VCPKG_UPDATE:-0}"
CLEAN_BUILD="${CLEAN_BUILD:-0}"
FORCE_RECOMPILE="${FORCE_RECOMPILE:-0}"   # Set to 1 to wipe dep stamps and rebuild from source

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: missing required command: $1" >&2
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Dependency verification helpers
# ---------------------------------------------------------------------------

# Returns 0 (true) if vcpkg repo is present and bootstrapped
vcpkg_exists() {
    [ -d "$VCPKG_ROOT/.git" ] && [ -x "$VCPKG_ROOT/vcpkg" ]
}

# Returns 0 (true) if a FetchContent dependency source directory already exists
fetchcontent_dep_exists() {
    dep_name="$1"   # e.g. "nlohmann_json", "cpp_httplib", "mcp_sdk"
    [ -d "$BUILD_DIR/_deps/${dep_name}-src" ]
}

verify_dependencies() {
    echo "-- Verifying cached dependencies..."
    for dep in nlohmann_json cpp_httplib mcp_sdk; do
        if fetchcontent_dep_exists "$dep"; then
            echo "   [ok]  $dep (cached at $BUILD_DIR/_deps/${dep}-src)"
        else
            echo "   [--]  $dep not yet downloaded (will be fetched by CMake)"
        fi
    done
}

# ---------------------------------------------------------------------------
# vcpkg setup — clone and bootstrap only when missing
# ---------------------------------------------------------------------------
prepare_vcpkg_repo() {
    if [ ! -d "$VCPKG_ROOT/.git" ]; then
        echo "-- vcpkg not found at $VCPKG_ROOT, cloning..."
        mkdir -p "$(dirname "$VCPKG_ROOT")"
        git clone "$VCPKG_REPO_URL" "$VCPKG_ROOT"
    else
        echo "-- vcpkg repo already present at $VCPKG_ROOT, skipping clone"
    fi

    git config --global --add safe.directory "$VCPKG_ROOT" >/dev/null 2>&1 || true

    if [ "$(git -C "$VCPKG_ROOT" rev-parse --is-shallow-repository 2>/dev/null || echo false)" = "true" ]; then
        echo "-- Converting shallow vcpkg clone to full clone..."
        if ! git -C "$VCPKG_ROOT" fetch --unshallow --tags --prune; then
            git -C "$VCPKG_ROOT" fetch --tags --prune --depth=2147483647
        fi
    fi

    if [ "$VCPKG_UPDATE" = "1" ]; then
        echo "-- Updating vcpkg metadata (VCPKG_UPDATE=1)"
        git -C "$VCPKG_ROOT" fetch --tags --prune || true
    else
        echo "-- Skipping vcpkg metadata update (set VCPKG_UPDATE=1 to refresh)"
    fi
}

bootstrap_vcpkg() {
    if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
        echo "-- Bootstrapping vcpkg..."
        sh "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
    else
        echo "-- vcpkg already bootstrapped, skipping"
    fi
}

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------
require_cmd git
require_cmd cmake
require_cmd c++
require_cmd make

prepare_vcpkg_repo
bootstrap_vcpkg

CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
if [ ! -f "$CMAKE_TOOLCHAIN_FILE" ]; then
    echo "error: vcpkg toolchain file not found: $CMAKE_TOOLCHAIN_FILE" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Clean build
# ---------------------------------------------------------------------------
if [ "$CLEAN_BUILD" = "1" ]; then
    echo "-- CLEAN_BUILD=1, removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# ---------------------------------------------------------------------------
# Force recompile — removes only FetchContent stamps, not the entire build dir.
# Lets vcpkg-managed libs and your own compiled objects survive.
# ---------------------------------------------------------------------------
if [ "$FORCE_RECOMPILE" = "1" ]; then
    echo "-- FORCE_RECOMPILE=1, wiping FetchContent dependency caches..."
    for dep in nlohmann_json cpp_httplib mcp_sdk; do
        dep_dir="$BUILD_DIR/_deps/${dep}-subbuild"
        src_dir="$BUILD_DIR/_deps/${dep}-src"
        bld_dir="$BUILD_DIR/_deps/${dep}-build"
        rm -rf "$dep_dir" "$src_dir" "$bld_dir" 2>/dev/null || true
        echo "   wiped: $dep"
    done
    echo "-- Done wiping caches. CMake will re-download and recompile these deps."
fi

# ---------------------------------------------------------------------------
# Show what's cached before configuring
# ---------------------------------------------------------------------------
verify_dependencies

# ---------------------------------------------------------------------------
# CMake configure — skip if already configured and nothing changed.
# CMake itself detects staleness via its own cache; we only re-run configure
# when the build dir is new or CLEAN_BUILD/FORCE_RECOMPILE wiped it.
# ---------------------------------------------------------------------------
CMAKE_EXTRA_FLAGS="-DFORCE_RECOMPILE=OFF"
if [ "$FORCE_RECOMPILE" = "1" ]; then
    # Stamps were already wiped above; pass OFF so CMake doesn't wipe again on
    # subsequent incremental builds from the same shell session.
    CMAKE_EXTRA_FLAGS="-DFORCE_RECOMPILE=OFF"
fi

if command -v ninja >/dev/null 2>&1; then
    echo "-- Configuring with Ninja generator"
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
        -DVCPKG_MANIFEST_DIR="$VCPKG_MANIFEST_DIR" \
        $CMAKE_EXTRA_FLAGS
else
    echo "-- Configuring with Unix Makefiles generator"
    cmake -S . -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
        -DVCPKG_MANIFEST_DIR="$VCPKG_MANIFEST_DIR" \
        $CMAKE_EXTRA_FLAGS
fi

cmake --build "$BUILD_DIR" --parallel "$JOBS"