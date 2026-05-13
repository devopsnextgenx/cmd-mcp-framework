#!/bin/sh

set -eu

VCPKG_ROOT="${VCPKG_ROOT:-/usr/local/vcpkg}"
CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
VCPKG_MANIFEST_DIR="$PWD"

rm -rf build
cmake -S . -B build \
	-DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
	-DVCPKG_MANIFEST_DIR="$VCPKG_MANIFEST_DIR"
cmake --build build -j
