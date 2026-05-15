FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive

# Install C++ tools and dependencies
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        ca-certificates \
        pkg-config \
        sudo \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Create vscode user with sudo access
RUN groupadd --gid 1000 vscode \
    && useradd --uid 1000 --gid 1000 -m vscode \
    && mkdir -p /etc/sudoers.d \
    && echo "vscode ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/vscode \
    && chmod 0440 /etc/sudoers.d/vscode

# Set git defaults
RUN git config --global user.name "Amit Kshirsagar" && \
    git config --global user.email "amit.kshirsagar.13@gmail.com"

# Create vcpkg directory with proper permissions before switching users
RUN mkdir -p /usr/local/vcpkg && \
    chown -R vscode:vscode /usr/local/vcpkg

USER vscode

# Set the working directory
WORKDIR /workspace

# Expose the ports your project uses
EXPOSE 4173 6543 5432 5433