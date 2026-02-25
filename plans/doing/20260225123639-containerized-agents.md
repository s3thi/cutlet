# Containerized Agents

## Objective

Add Docker-based containerization so Claude Code agents can execute plans with `--dangerously-skip-permissions` in full isolation from the host machine. The container clones the host's local repo, works on a branch, and pushes the branch back when done.

When done:

- A Dockerfile builds an image with the full Cutlet build toolchain (C23, clang-format, clang-tidy, bear, python3, universal-ctags, cscope) plus Claude Code.
- `scripts/agent-build` builds the Docker image.
- `scripts/agent-run <plan-slug>` starts a container that clones the host repo, checks out (or creates) a branch for the plan, runs Claude Code on the next incomplete step, commits, and pushes the branch back.
- `scripts/agent-merge <branch>` merges a finished agent branch into main and deletes the branch.
- `scripts/agent-list` shows running agent containers.
- `scripts/agent-logs <plan-slug>` tails the logs of a running agent container.
- `scripts/agent-stop <plan-slug>` stops a running agent container.
- Running `scripts/agent-run` multiple times on a multi-step plan advances through each step, because each run picks up where the previous push left off.

## Acceptance criteria

- [ ] Dockerfile builds successfully and produces a working image with all build tools and Claude Code.
- [ ] `make test && make check` pass inside the container (verifying the toolchain works).
- [ ] `scripts/agent-build` builds the image and tags it `cutlet-agent:latest`.
- [ ] `scripts/agent-run <plan-slug>` starts a detached container that: clones the host repo, finds the plan file, creates/checks out the branch, runs Claude Code with `--dangerously-skip-permissions`, and pushes the branch on exit.
- [ ] `scripts/agent-merge <branch>` merges a branch into main, runs `make test && make check`, and deletes the branch on success.
- [ ] `scripts/agent-list` lists running cutlet-agent containers with their plan slug and uptime.
- [ ] `scripts/agent-logs <plan-slug>` shows live output from a running agent.
- [ ] `scripts/agent-stop <plan-slug>` stops a running agent container gracefully.
- [ ] All scripts are executable, have usage help (`-h` or no args), and handle errors with clear messages.
- [ ] `.dockerignore` excludes `build/`, `build-sanitize/`, and `compile_commands.json`.

## Dependencies

None. This is infrastructure, not a language feature.

## Constraints and non-goals

- **No orchestration.** No Docker Compose, no auto-scheduling. The user manages parallelism manually by running `agent-run` multiple times.
- **No remote push.** Containers push to the local host repo only (via mounted `.git` directory), not to GitHub.
- **No persistent containers.** Each `agent-run` invocation creates a fresh, ephemeral container. State is preserved through git (branches pushed to host repo).
- **Planning stays local.** These tools are for *executing* plans, not creating them.
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

### Branch lifecycle

1. First `agent-run maps` → container creates branch `maps`, works on step 1, pushes `maps`.
2. Second `agent-run maps` → container clones, checks out `origin/maps`, works on step 2, pushes `maps`.
3. Repeat until all steps are complete.
4. `agent-merge maps` → merges `maps` into `main`, deletes the branch.

### Container naming

Containers are named `cutlet-agent-<plan-slug>` (e.g., `cutlet-agent-maps`). This makes `agent-list`, `agent-logs`, and `agent-stop` straightforward. If a container with the same name already exists (from a previous run), the old container is removed before starting a new one.

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

**No ENTRYPOINT or CMD** — the `agent-run` script provides the command.

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

### Step 3: `scripts/agent-run`

The core script. Starts a detached container to execute one plan step.

```
Usage: scripts/agent-run <plan-slug> [--max-turns N]
```

Where `<plan-slug>` is the plan identifier (e.g., `maps`, `in-operator`). The optional `--max-turns` defaults to 200 (a safety cap).

Behavior:

1. **Find the plan file**: glob for `plans/doing/*-<slug>.md`. Error if not found or ambiguous.
2. **Derive names**: container name is `cutlet-agent-<slug>`, branch name is `<slug>`.
3. **Clean up previous container**: if a container with the same name exists (stopped), remove it with `docker rm`.
4. **Resolve host paths**:
   - `REPO_GIT_DIR`: the absolute path to the repo's `.git` directory.
   - `CLAUDE_DIR`: `$HOME/.claude` (the user's Claude credentials directory).
5. **Start the container** (detached):

   ```bash
   docker run -d \
     --name cutlet-agent-$SLUG \
     -v "$REPO_GIT_DIR":/host-repo.git \
     -v "$CLAUDE_DIR":/root/.claude:ro \
     cutlet-agent:latest \
     /bin/bash -c "$ENTRYPOINT_SCRIPT"
   ```

6. **Entrypoint script** (passed as a string to bash -c):

   ```bash
   set -euo pipefail

   # Clone from the mounted host repo .git directory
   git clone /host-repo.git /workspace
   cd /workspace

   # Find the plan file
   PLAN_FILE=$(ls plans/doing/*-SLUG.md 2>/dev/null | head -1)

   # Create or check out the branch
   if git branch -r | grep -q "origin/BRANCH"; then
     git checkout -b BRANCH origin/BRANCH
   else
     git checkout -b BRANCH
   fi

   # Configure git for commits
   git config user.name "Cutlet Agent"
   git config user.email "agent@cutlet.dev"

   # Run Claude Code
   claude -p "PROMPT_TEXT" \
     --dangerously-skip-permissions \
     --max-turns MAX_TURNS \
     --verbose

   # Push the branch back (even if Claude Code exits non-zero, try to push any commits)
   git push origin BRANCH 2>/dev/null || true
   ```

   The script substitutes `SLUG`, `BRANCH`, `PLAN_FILE`, `MAX_TURNS`, and `PROMPT_TEXT` before passing to the container.

7. **Prompt text** for Claude Code (embedded in the entrypoint):

   ```
   Read AGENTS.md for project conventions — follow them exactly.

   Read the plan file at <PLAN_FILE> and execute it.

   If the plan has multiple implementation steps:
   1. Check the ## Progress section at the bottom of the plan to see what's done.
   2. Work on the next incomplete step.
   3. After completing the step, update the ## Progress section and commit.
   4. Stop after completing one step.

   If the plan has no steps or a single step, complete the entire plan.

   After all work is done, commit your changes.
   ```

8. **Output**: print the container name and how to follow along:
   ```
   Started cutlet-agent-maps
   Logs: scripts/agent-logs maps
   Stop: scripts/agent-stop maps
   ```

**File to create**: `scripts/agent-run`

### Step 4: `scripts/agent-merge`

Merges a finished agent branch into main and cleans up.

```
Usage: scripts/agent-merge <branch-name>
```

Behavior:

1. Verify current branch is `main`. If not, error.
2. Verify `<branch-name>` exists as a local branch. If not, error.
3. Check for uncommitted changes in main. If dirty, error.
4. Run `git merge <branch> --no-edit`.
5. If conflicts, print them and exit non-zero (user resolves manually).
6. Run `make test && make check`. If either fails, print the error and exit non-zero (user fixes manually).
7. Delete the branch: `git branch -d <branch>`.
8. Print summary: merge commit hash, branch deleted.

This is intentionally simpler than the `cutlet-merge-worktree` command because there are no worktree directories to clean up, and the user should handle conflicts themselves rather than having an agent resolve them.

**File to create**: `scripts/agent-merge`

### Step 5: `scripts/agent-list`

Lists running cutlet-agent containers.

```
Usage: scripts/agent-list
```

Behavior:
- Run `docker ps --filter "name=cutlet-agent-" --format "table {{.Names}}\t{{.Status}}\t{{.RunningFor}}"`.
- If no containers are running, print "No running agents."

**File to create**: `scripts/agent-list`

### Step 6: `scripts/agent-logs`

Shows live output from a running agent container.

```
Usage: scripts/agent-logs <plan-slug>
```

Behavior:
- Run `docker logs -f cutlet-agent-<slug>`.
- If the container doesn't exist, print an error.

**File to create**: `scripts/agent-logs`

### Step 7: `scripts/agent-stop`

Stops a running agent container.

```
Usage: scripts/agent-stop <plan-slug>
```

Behavior:
- Run `docker stop cutlet-agent-<slug>`.
- Run `docker rm cutlet-agent-<slug>`.
- Print confirmation.

**File to create**: `scripts/agent-stop`

### Step 8: Manual verification

Since this is infrastructure (not language source code), verification is manual rather than automated:

1. Run `scripts/agent-build` — verify the image builds and `make test && make check` pass during the build.
2. Run `scripts/agent-run` with a simple test plan — verify the container starts, clones the repo, and Claude Code connects to the API.
3. Run `scripts/agent-list` — verify the running container shows up.
4. Run `scripts/agent-logs` — verify live output is visible.
5. Run `scripts/agent-stop` — verify the container stops and is removed.
6. After a successful agent run, verify the branch was pushed to the host repo (`git branch` on the host should show the new branch).
7. Run `scripts/agent-merge` — verify the branch is merged and deleted.

---

## Notes for the executing agent

- All scripts should use `#!/usr/bin/env bash` and `set -euo pipefail`.
- All scripts should print usage when called with no arguments or `-h`.
- Prefer `printf` over `echo` for portable output.
- The Dockerfile should pin versions where possible (e.g., `gcc-14`, `clang-format-18`) to avoid surprises.
- The git config inside the container (`user.name`, `user.email`) is for commit metadata only — these don't need to match the host user's identity.

---
End of plan.
