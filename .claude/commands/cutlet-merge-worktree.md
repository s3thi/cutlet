Merge a finished agent worktree into main, then clean up.

The argument below is the worktree/branch name (e.g., `anonymous-functions`).

$ARGUMENTS

Follow these steps in order. Stop and ask me only when explicitly noted below.

## 0. Determine paths

- Get the main worktree root: `git rev-parse --show-toplevel`
- The parent directory of that root is where all worktrees live (they are sibling directories).
- The target worktree is at `<parent>/<branch-name>`.
- Use these derived paths throughout — never hardcode absolute paths.

## 1. Validate state

- Confirm the current branch is `main` (`git symbolic-ref --short HEAD`). If not, refuse to proceed.
- Confirm the target worktree directory exists and appears in `git worktree list`.
- Check for uncommitted changes in main (`git status --porcelain`). If dirty, stop and tell me.
- Check for uncommitted changes in the target worktree (`git -C <worktree-path> status --porcelain`). If dirty, warn me and list the uncommitted files. Ask whether to proceed — those changes will be lost.

## 2. Merge

- Run `git merge <branch> --no-edit`.
- If there are conflicts:
  - List conflicted files.
  - For each conflicted file, read it and understand both sides' intent by examining the branch's commit log.
  - Resolve each conflict, preserving the intent of both sides. If a commit adds new syntax or a feature, that feature must continue to work after resolution.
  - Stage resolved files and commit.
- If the merge is clean, proceed silently.

## 3. Verify and fix

- Run `make test` and `make check`.
- If either fails, read the errors, fix them, and re-run until both pass.
- If you cannot fix a failure after two attempts, stop and ask me for help.

## 4. Check for completed plan files

- Look at `plans/doing/` for any task files that correspond to the merged branch's work (match by topic/feature name).
- If you find a plausible match, ask me if the task is complete. If yes, move it to `plans/done/` with the next consecutive 4-digit number prefix and add a short summary of what changed.
- It's possible that the plan files have already been moved to the correct location. There's no need to perform this step if the git log shows the files being moved.
- **Fix conflicting numbering.** After the merge, check `plans/done/` for duplicate number prefixes (e.g., two files both starting with `0020-`). If duplicates exist, use `git log` timestamps to determine which file was moved to `done/` first (earliest commit timestamp keeps its number). Renumber the other file(s) so all prefixes are unique and consecutive. There must never be gaps or duplicates in the numbering sequence.

## 5. Clean up

- Remove the worktree: `git worktree remove <worktree-path>`
- Delete the branch: `git branch -d <branch>`
- Show me the final `git worktree list` and `git log --oneline -5`.
