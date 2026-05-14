#!/bin/sh

set -eu

VCPKG_ROOT="${VCPKG_ROOT:-/usr/local/vcpkg}"
VCPKG_REPO_URL="${VCPKG_REPO_URL:-https://github.com/microsoft/vcpkg.git}"
BUILD_DIR="${BUILD_DIR:-build}"
VCPKG_MANIFEST_DIR="${VCPKG_MANIFEST_DIR:-$PWD}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
VCPKG_UPDATE="${VCPKG_UPDATE:-0}"
CLEAN_BUILD="${CLEAN_BUILD:-0}"

require_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "error: missing required command: $1" >&2
		exit 1
	fi
}

prepare_vcpkg_repo() {
	if [ ! -d "$VCPKG_ROOT/.git" ]; then
		echo "-- vcpkg not found at $VCPKG_ROOT, cloning..."
		mkdir -p "$(dirname "$VCPKG_ROOT")"
		git clone "$VCPKG_REPO_URL" "$VCPKG_ROOT"
	fi

	# /usr/local/vcpkg is commonly root-owned in dev containers.
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
	fi
}

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

if [ "$CLEAN_BUILD" = "1" ]; then
	echo "-- CLEAN_BUILD=1, removing $BUILD_DIR"
	rm -rf "$BUILD_DIR"
fi

if command -v ninja >/dev/null 2>&1; then
	echo "-- Configuring with Ninja generator"
	cmake -S . -B "$BUILD_DIR" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
		-DVCPKG_MANIFEST_DIR="$VCPKG_MANIFEST_DIR"
else
	echo "-- Configuring with Unix Makefiles generator"
	cmake -S . -B "$BUILD_DIR" \
		-DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
		-DVCPKG_MANIFEST_DIR="$VCPKG_MANIFEST_DIR"
fi

cmake --build "$BUILD_DIR" --parallel "$JOBS"
