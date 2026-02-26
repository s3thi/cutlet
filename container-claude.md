# Container environment

You are running inside a Docker container. The host machine is not accessible.

- **Workspace**: `/workspace` — a git clone of the host repo. All work happens here.
- **Branch**: The container was started on a specific branch. Check `git branch` to see which one.
- **Git remote**: `origin` points to the host repo's `.git` directory (mounted at `/host-repo.git`). Push your branch with `git push origin <branch>` when work is ready for merging.
- **Permissions**: You are running with `--dangerously-skip-permissions` in full isolation. The container cannot affect the host beyond the mounted `.git` directory. You have full internet access, passwordless `sudo`, and all permissions. Install packages, download tools, do whatever you need.
- **Toolchain**: gcc-14, clang-format-18, clang-tidy-18, bear, python3, universal-ctags, cscope, make, git, tmux. Install anything else you need with `apt-get`.

## Workflow

1. Use `/cutlet-execute <plan-name>` to implement a plan step-by-step, or `/cutlet-execute-autopilot <plan-name>` to run all steps unattended.
2. After completing work, push the branch: `git push origin <branch>`.
3. The user will merge on the host with `/cutlet-merge-agent <branch>`.
