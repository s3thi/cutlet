# Containerized Agents

## Objective

Add Docker-based containerization so Claude Code can run with `--dangerously-skip-permissions` in full isolation from the host machine. The user starts a container, gets dropped into a tmux session with Claude Code and a shell, and works interactively. The container clones the host's local repo onto a branch and pushes back when instructed.

When done:

- A Dockerfile builds an image with the full Cutlet build toolchain (C23, clang-format, clang-tidy, bear, python3, universal-ctags, cscope), tmux, and Claude Code.
- `scripts/agent-build` builds the Docker image.
- `scripts/agent-start <branch>` starts a new container, clones the repo, creates the branch, and drops the user into a tmux session with two panes: Claude Code (with `--dangerously-skip-permissions`) and a shell.
- `scripts/agent-list` shows all agent containers (running and paused).
- `scripts/agent-pause <branch>` pauses a running container (freezes all processes, releases CPU).
- `scripts/agent-connect <branch>` resumes a paused container and reattaches to its tmux session.
- `scripts/agent-delete <branch>` permanently removes a container.

## Acceptance criteria

- [ ] Dockerfile builds successfully and produces a working image with all build tools, tmux, and Claude Code.
- [ ] `make test && make check` pass inside the container (verifying the toolchain works).
- [ ] `scripts/agent-build` builds the image and tags it `cutlet-agent:latest`.
- [ ] `scripts/agent-start <branch>` creates a container, clones the repo, creates the branch, and drops the user into a tmux session with Claude Code + shell panes.
- [ ] Detaching from tmux (Ctrl+B, D) leaves the container running. Disconnecting the terminal also leaves the container running.
- [ ] `scripts/agent-list` lists all cutlet-agent containers with their status (running/paused) and uptime.
- [ ] `scripts/agent-pause <branch>` pauses a running container.
- [ ] `scripts/agent-connect <branch>` resumes a paused container and reattaches to tmux.
- [ ] `scripts/agent-delete <branch>` stops and removes a container.
- [ ] All scripts are executable, have usage help (`-h` or no args), and handle errors with clear messages.
- [ ] `.dockerignore` excludes `build/`, `build-sanitize/`, and `compile_commands.json`.

## Dependencies

None. This is infrastructure, not a language feature.

## Constraints and non-goals

- **No orchestration.** No Docker Compose, no auto-scheduling. The user manages containers manually.
- **No remote push.** Containers push to the local host repo only (via mounted `.git` directory), not to GitHub. The user pushes to GitHub from the host.
- **General-purpose.** The container is a dev environment, not tied to any specific workflow. The user decides what to tell Claude.
- **macOS host assumed.** The Docker host is macOS (Darwin). Linux host should also work but isn't the primary target.
- **Claude Max/Pro OAuth only.** Authentication is via `~/.claude/` mounted into the container. API key users would need a small tweak (pass `ANTHROPIC_API_KEY` env var instead).
- **Not source code.** These are developer tooling scripts. `make test && make check` do not need to run on the host after changes to these files. The Dockerfile should verify the toolchain works by running `make test && make check` during the image build.

---

## Architecture

### How the container interacts with the host

```
Host machine                          Docker container
─────────────                         ────────────────
~/.claude/  ──mount (ro)──────────►  /root/.claude/
                                      (OAuth credentials)

<repo>/.git ──mount (rw)──────────►  /host-repo.git
                                      (used as git remote)

                                     /workspace/
                                      (git clone of /host-repo.git)
                                      (Claude Code works here)
                                      (pushes branch back to /host-repo.git)
```

**Why mount `.git` instead of the whole repo?** The container only needs the git database to clone from and push to. Mounting just `.git` prevents the container from touching the host's working tree. The mount is read-write because `git push` writes objects and refs to the remote.

**Why mount `~/.claude/` read-only?** The container needs OAuth tokens to authenticate with the Anthropic API. Read-only prevents the container from modifying credentials.

### Container lifecycle

1. `agent-start foo` → creates container `cutlet-agent-foo`, clones repo, creates branch `foo`, starts tmux with Claude Code + shell panes, user is attached.
2. User detaches from tmux (Ctrl+B, D) → container keeps running in the background.
3. `agent-pause foo` → freezes the container (saves memory/CPU).
4. `agent-connect foo` → resumes the container and reattaches to tmux.
5. `agent-delete foo` → permanently removes the container.

### Container naming

Containers are named `cutlet-agent-<branch>` (e.g., `cutlet-agent-foo`). This makes all lifecycle scripts straightforward — they just need the branch name to find the container.

### tmux layout

The tmux session is named `main` and has two panes in a vertical split (side by side):

- **Left pane**: `claude --dangerously-skip-permissions` running in `/workspace`.
- **Right pane**: A shell (`bash`) in `/workspace`.

The user can resize, rearrange, or create additional panes as needed.

---

## Implementation steps

### Step 1: Dockerfile and .dockerignore

Create `Dockerfile` at the repo root.

**Base image**: `ubuntu:24.04`.

**System packages** (via apt):
- `gcc-14`, `g++-14` — C23 compiler
- `make` — build system
- `clang-format-18`, `clang-tidy-18` — formatting and linting
- `bear` — compile_commands.json generator
- `python3` — analysis scripts
- `universal-ctags`, `cscope` — codebase understanding tools
- `git` — version control
- `curl`, `ca-certificates` — for downloading Node.js
- `nodejs`, `npm` — for Claude Code (install via NodeSource or apt)
- `tmux` — terminal multiplexer

**Symlinks** for tool names the Makefile expects:
- `gcc-14` → `cc` (or set `CC=gcc-14`)
- `clang-format-18` → `clang-format`
- `clang-tidy-18` → `clang-tidy`

**Claude Code**: `npm install -g @anthropic-ai/claude-code`

**Verification layer** (build-time check):
- Copy the repo source into a temp directory during the build.
- Run `make test` to verify the toolchain works.
- Discard the temp copy (it's just for verification; the real source comes from git clone at runtime).

**Working directory**: `/workspace`

**No ENTRYPOINT or CMD** — the `agent-start` script provides the command.

Create `.dockerignore` at the repo root:
```
build/
build-sanitize/
compile_commands.json
.git/
```

**Files to create**: `Dockerfile`, `.dockerignore`

### Step 2: `scripts/agent-build`

Shell script that builds the Docker image.

```
Usage: scripts/agent-build
```

Behavior:
- Run `docker build -t cutlet-agent:latest .` from the repo root.
- Pass `--progress=plain` so build output is visible.
- Print success/failure message.
- Exit with docker's exit code.

**File to create**: `scripts/agent-build`

### Step 3: `scripts/agent-start`

The core script. Creates a new container and drops the user into tmux.

```
Usage: scripts/agent-start <branch-name>
```

Where `<branch-name>` is any valid git branch name (e.g., `foo`, `fix-parser-bug`, `refactor-vm`).

Behavior:

1. **Validate**: error if a container named `cutlet-agent-<branch>` already exists (running or paused). The user must `agent-delete` it first or `agent-connect` to it.
2. **Resolve host paths and git identity**:
   - `REPO_GIT_DIR`: the absolute path to the repo's `.git` directory.
   - `CLAUDE_DIR`: `$HOME/.claude` (the user's Claude credentials directory).
   - `GIT_USER_NAME`: from `git config user.name` in the host repo.
   - `GIT_USER_EMAIL`: from `git config user.email` in the host repo.
3. **Start the container** (detached):

   ```bash
   docker run -d \
     --name cutlet-agent-$BRANCH \
     -e GIT_USER_NAME="$GIT_USER_NAME" \
     -e GIT_USER_EMAIL="$GIT_USER_EMAIL" \
     -v "$REPO_GIT_DIR":/host-repo.git \
     -v "$CLAUDE_DIR":/root/.claude:ro \
     cutlet-agent:latest \
     /bin/bash -c "$ENTRYPOINT_SCRIPT"
   ```

4. **Entrypoint script** (passed as a string to bash -c):

   ```bash
   set -euo pipefail

   # Clone from the mounted host repo .git directory
   git clone /host-repo.git /workspace
   cd /workspace

   # Create or check out the branch
   if git branch -r | grep -q "origin/BRANCH"; then
     git checkout -b BRANCH origin/BRANCH
   else
     git checkout -b BRANCH
   fi

   # Configure git for commits (identity passed in from host via env vars)
   git config user.name "$GIT_USER_NAME"
   git config user.email "$GIT_USER_EMAIL"

   # Start a tmux session with two panes:
   #   Left: claude --dangerously-skip-permissions
   #   Right: bash shell
   tmux new-session -d -s main -c /workspace \
     'claude --dangerously-skip-permissions'
   tmux split-window -h -t main -c /workspace

   # Keep the container alive (tmux server runs in background)
   sleep infinity
   ```

5. **Wait for setup**: poll briefly (e.g., `docker exec ... tmux has-session -t main`) until the tmux session is ready.

6. **Attach**: `docker exec -it cutlet-agent-$BRANCH tmux attach -t main`.

When the user detaches from tmux (Ctrl+B, D), the `docker exec` ends but the container stays running because `sleep infinity` is PID 1.

**File to create**: `scripts/agent-start`

### Step 4: `scripts/agent-list`

Lists all cutlet-agent containers (running and paused).

```
Usage: scripts/agent-list
```

Behavior:
- Run `docker ps -a --filter "name=cutlet-agent-" --format "table {{.Names}}\t{{.Status}}"`.
- If no containers exist, print "No agent containers."

**File to create**: `scripts/agent-list`

### Step 5: `scripts/agent-pause`

Pauses a running container (freezes all processes).

```
Usage: scripts/agent-pause <branch-name>
```

Behavior:
- Verify container `cutlet-agent-<branch>` exists and is running.
- Run `docker pause cutlet-agent-<branch>`.
- Print confirmation.

**File to create**: `scripts/agent-pause`

### Step 6: `scripts/agent-connect`

Resumes a paused container and reattaches to its tmux session. Also works for running containers that the user simply disconnected from.

```
Usage: scripts/agent-connect <branch-name>
```

Behavior:
- Verify container `cutlet-agent-<branch>` exists.
- If paused, run `docker unpause cutlet-agent-<branch>`.
- Run `docker exec -it cutlet-agent-<branch> tmux attach -t main`.
- If the tmux session is gone (e.g., user killed it), print an error suggesting `agent-delete` and `agent-start`.

**File to create**: `scripts/agent-connect`

### Step 7: `scripts/agent-delete`

Permanently removes a container.

```
Usage: scripts/agent-delete <branch-name>
```

Behavior:
- Run `docker rm -f cutlet-agent-<branch>`.
- Print confirmation.

**File to create**: `scripts/agent-delete`

### Step 8: Manual verification

Since this is infrastructure (not language source code), verification is manual rather than automated:

1. Run `scripts/agent-build` — verify the image builds and `make test && make check` pass during the build.
2. Run `scripts/agent-start test-branch` — verify the container starts, tmux appears with Claude Code in the left pane and a shell in the right pane.
3. In the shell pane, verify `git branch` shows `test-branch` and the repo is cloned correctly.
4. Detach from tmux (Ctrl+B, D) — verify the container is still running (`agent-list`).
5. Run `scripts/agent-pause test-branch` — verify the container is paused (`agent-list` shows paused status).
6. Run `scripts/agent-connect test-branch` — verify it resumes and reattaches to the same tmux session.
7. Run `scripts/agent-delete test-branch` — verify the container is removed (`agent-list` shows nothing).

---

## Notes for the executing agent

- All scripts should use `#!/usr/bin/env bash` and `set -euo pipefail`.
- All scripts should print usage when called with no arguments or `-h`.
- Prefer `printf` over `echo` for portable output.
- The Dockerfile should pin versions where possible (e.g., `gcc-14`, `clang-format-18`) to avoid surprises.
- The git config inside the container (`user.name`, `user.email`) is read from the host repo's git config at runtime and passed in as environment variables, so commits are attributed to the real user.

---

## Progress

- [x] Step 1: Dockerfile and .dockerignore — created Dockerfile (Ubuntu 24.04, gcc-14, clang-format-18, clang-tidy-18, bear, python3, ctags, cscope, Node.js 20, Claude Code, build-time `make test` verification) and .dockerignore (excludes build/, build-sanitize/, compile_commands.json, .git/)
- [x] Step 2: `scripts/agent-build` — created build script that runs `docker build -t cutlet-agent:latest --progress=plain` from repo root, with `-h` usage help
- [x] Step 3: `scripts/agent-start` — created start script that creates a container, clones repo from mounted .git, creates/checks out branch, configures git identity, starts tmux with Claude Code + shell panes, waits for tmux ready, and attaches
- [x] Step 4: `scripts/agent-list` — created list script that shows all cutlet-agent containers with status/uptime, or prints "No agent containers." when none exist

---
End of plan.
