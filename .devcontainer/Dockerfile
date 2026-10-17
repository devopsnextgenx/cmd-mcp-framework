FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Install build/runtime dependencies needed by this repo.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        software-properties-common \
        gnupg \
        cmake \
        ninja-build \
        build-essential \
        ccache \
        pkg-config \
        curl \
        ca-certificates \
    && add-apt-repository -y ppa:deadsnakes/ppa \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        python3.13 \
        python3.13-venv \
    && rm -rf /var/lib/apt/lists/*

# Set git defaults
RUN git config --global user.name "Amit Kshirsagar" && \
    git config --global user.email "amit.kshirsagar.13@gmail.com"
    
# Expose ports for MCP server and dashboard
EXPOSE 4173 6543 5432 5433
