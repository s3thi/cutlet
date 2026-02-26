# Cutlet development environment for containerized Claude Code agents.
#
# Includes the full Cutlet build toolchain (C23, clang-format, clang-tidy,
# bear, python3, universal-ctags, cscope), tmux, and Claude Code.
#
# Build:  scripts/agent-build
# Usage:  scripts/agent-start <branch>

FROM ubuntu:24.04

# Avoid interactive prompts during package installation.
ENV DEBIAN_FRONTEND=noninteractive

# Install system packages:
#   gcc-14, g++-14   — C23 compiler
#   make             — build system
#   clang-format-18, clang-tidy-18 — formatting and linting
#   bear             — compile_commands.json generator
#   python3          — analysis scripts
#   universal-ctags, cscope — codebase understanding tools
#   curl, ca-certificates — for downloading Node.js and adding PPAs
#   software-properties-common — for add-apt-repository
#   tmux             — terminal multiplexer
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-14 \
    g++-14 \
    make \
    clang-format-18 \
    clang-tidy-18 \
    bear \
    python3 \
    universal-ctags \
    cscope \
    curl \
    ca-certificates \
    software-properties-common \
    tmux \
    && rm -rf /var/lib/apt/lists/*

# Install latest stable git from the git-core PPA. Ubuntu 24.04 ships
# git 2.43, which doesn't support the relativeWorktrees extension
# (added in git 2.46). Hosts with newer git may use this extension in
# worktree-based repos, so the container needs a matching version.
RUN add-apt-repository -y ppa:git-core/ppa && \
    apt-get update && \
    apt-get install -y --no-install-recommends git && \
    rm -rf /var/lib/apt/lists/*

# Create symlinks so tool names match what the Makefile expects:
#   gcc-14           -> cc     (Makefile uses CC ?= cc)
#   clang-format-18  -> clang-format
#   clang-tidy-18    -> clang-tidy
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100

# Install Node.js 20 LTS via NodeSource (required for Claude Code).
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get install -y --no-install-recommends nodejs && \
    rm -rf /var/lib/apt/lists/*

# Install Claude Code globally.
RUN npm install -g @anthropic-ai/claude-code

# Verification: copy repo source, run make test to prove the toolchain works,
# then discard the copy. The real source comes from git clone at runtime.
COPY . /tmp/cutlet-verify
RUN cd /tmp/cutlet-verify && make test && rm -rf /tmp/cutlet-verify

# Create a non-root user. Claude Code refuses --dangerously-skip-permissions
# when running as root for security reasons.
RUN useradd -m -s /bin/bash agent

WORKDIR /workspace
