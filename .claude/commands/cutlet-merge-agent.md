Merge a finished agent container's branch into main, then clean up.

The argument below is the branch name (e.g., `fix-parser-bug`).

$ARGUMENTS

Follow these steps in order. Stop and ask me only when explicitly noted below.

## 1. Validate state

- Confirm the current branch is `main` (`git symbolic-ref --short HEAD`). If not, refuse to proceed.
- Confirm the target branch exists (`git branch --list <branch>`). If not, stop and tell me.
- Check for uncommitted changes in main (`git status --porcelain`). If dirty, stop and tell me.

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
- Run `make test-sanitize` (ASan + UBSan + LSan) and `make test-gc-stress`.
- If either fails, read the errors, fix them, and re-run until both pass.
- If you cannot fix a failure after two attempts, stop and ask me for help.

## 4. Check for completed plan files

- Look at `plans/doing/` for any task files that correspond to the merged branch's work (match by topic/feature name).
- If you find a plausible match, ask me if the task is complete. If yes, run `scripts/plan-done <name>` to move it to `plans/done/` with a fresh timestamp.
- It's possible that the plan files have already been moved to the correct location. There's no need to perform this step if the git log shows the files being moved.

## 5. Clean up

- Delete the branch: `git branch -d <branch>`
- Delete the container: `scripts/agent-delete <branch>`
- Show me the final `git log --oneline -5`.
