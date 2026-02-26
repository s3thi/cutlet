# Cutlet development environment for containerized Claude Code agents.
#
# Includes the full Cutlet build toolchain (C23, clang-format, clang-tidy,
# bear, python3, universal-ctags, cscope), tmux, and Claude Code.
#
# Build:  scripts/agent-build
# Usage:  scripts/agent-start <branch>
#
# Layer ordering: layers are ordered from least to most frequently changing
# so that Docker cache invalidation is minimal. The verification step (COPY +
# make test) is last because source code changes most often. Claude Code
# install and container-claude.md come before it so they stay cached across
# source edits.

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
#   curl, ca-certificates — for downloading Claude Code and adding PPAs
#   software-properties-common — for add-apt-repository
#   tmux             — terminal multiplexer
#   sudo             — passwordless root for agent user
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
    locales \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Generate a UTF-8 locale so terminal UI (Claude Code) renders correctly.
RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 TERM=xterm-256color

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

# Create a non-root user with passwordless sudo. Claude Code refuses
# --dangerously-skip-permissions as root, so it runs as 'agent' but
# has full root access via sudo for installing packages, etc.
# Write locale/terminal env vars into the agent user's profile so they
# survive `su -l` (which resets the environment from Docker ENV).
RUN useradd -m -s /bin/bash agent && \
    printf 'agent ALL=(ALL) NOPASSWD:ALL\n' > /etc/sudoers.d/agent && \
    printf 'export LANG=en_US.UTF-8\nexport LC_ALL=en_US.UTF-8\nexport TERM=xterm-256color\nexport PATH="$HOME/.local/bin:$PATH"\n' \
        >> /home/agent/.profile

# Install Claude Code via the native installer as the agent user.
# The installer places the binary at ~/.local/bin/claude and updates
# the user's shell profile to add it to PATH.
# To update Claude Code, rebuild the image (scripts/agent-build).
USER agent
RUN curl -fsSL https://claude.ai/install.sh | bash

# User-level CLAUDE.md — Claude Code loads this automatically from ~/.claude/.
# This gives every container session persistent context about the environment.
# Source of truth: container-claude.md in the repo root (copied here at build).
COPY container-claude.md /home/agent/.claude/CLAUDE.md
USER root

# Verification: copy repo source, run make test to prove the toolchain works,
# then discard the copy. The real source comes from git clone at runtime.
# This layer is last because source code changes most often — keeping it at
# the end means all layers above (packages, Claude Code) stay cached.
COPY . /tmp/cutlet-verify
RUN cd /tmp/cutlet-verify && make test && rm -rf /tmp/cutlet-verify

WORKDIR /workspace
