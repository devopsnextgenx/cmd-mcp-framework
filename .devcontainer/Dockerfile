FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Install build/runtime dependencies needed by this repo.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        build-essential \
        pkg-config \
        curl \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*
