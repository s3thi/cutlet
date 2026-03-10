Review code like a senior engineer reviewing a junior's work. You are looking for real problems — things that will cause bugs, confuse future readers (including AI agents), or make the codebase harder to change safely.

## Getting the code to review

Run `scripts/review-diff $ARGUMENTS` to get the list of changed files and the diff.

**Default** (no arguments): reviews working tree changes.
Other modes: `branch <name>`, `merge`, `last <N>`, `files <file1> [file2...]`.

After running the script:
1. Read the **full content** of every changed file listed (not just the diff). You need surrounding context to judge correctness.
2. For each changed file, also read its corresponding `.h` header if it exists, so you understand the public interface.

## What to look for

Think about these questions for each changed file. Only report genuine findings — if everything looks fine, say so.

### 1. "I don't understand this code"

The most important check. If you read a function and cannot confidently explain what it does and why, that is a finding. AI-generated code is especially prone to producing plausible-looking code that *works* but whose intent is opaque. The next agent that touches it will misunderstand and introduce a bug.

Flag: functions whose purpose is unclear, misleading names, logic that requires mental gymnastics to follow, control flow that obscures intent.

### 2. "What are the rules here?"

Look for implicit invariants — assumptions the code makes but doesn't document or enforce.

- "This function assumes X has already been called"
- "This field must be updated whenever that field changes"
- "The caller must free the returned value"
- "This pointer is never NULL at this point"

If an invariant is critical and not obvious, it should be either documented in a comment, enforced with an assert, or structurally impossible to violate. Flag cases where it's none of these.

### 3. "What happens when this fails?"

Trace the unhappy paths. Not "did you null-check malloc" (the sanitizer catches that) but the bigger picture:

- If this function fails partway through, is the caller's state consistent?
- If a builtin gets bad arguments, does the error propagate cleanly or does it leave garbage on the stack?
- Are error codes checked and meaningful, or silently swallowed?
- On the error path, are all resources cleaned up?

### 4. "This is going to bite us"

Look for fragile coupling and ticking time bombs:

- A function in one module reaching into the internals of another
- Two data structures that must stay in sync with no mechanism ensuring it
- Code that works today but will silently break when the next value type, opcode, or object type is added
- Hardcoded sizes or limits with no compile-time check

### 5. "This doesn't match the pattern"

Every codebase has conventions. When you read the surrounding code, you'll see patterns: how opcode handlers are structured, how builtins validate args, how errors are reported, how memory is allocated and freed. When the changed code breaks from an established pattern, flag it — either it's a bug or it's creating an inconsistency that will cause bugs later.

## What to ignore

Do NOT report any of the following. These are handled by other tools or are not useful.

- Formatting and style (clang-format handles this)
- "I would have written it differently" (preference, not a bug)
- Performance unless it's obviously O(n²) or worse on a hot path
- Test code quality — tests are allowed to be ugly and repetitive
- Missing documentation for self-evident code
- Things clang-tidy or the sanitizers already catch (null dereference, basic UB, memory leaks exercised by tests)
- Suggestions to add features, refactor for future extensibility, or improve abstractions. Only flag what's broken or fragile NOW.

## Output format

Organize findings by severity:

### Bugs
Things that are wrong today — incorrect behavior, data corruption, crashes.

### Risks
Things that aren't broken yet but are likely to break — implicit invariants, fragile coupling, time bombs.

### Clarity
Code that works but will mislead the next person (or agent) who reads it.

For each finding, include:
- **File and function** (e.g., `src/vm.c:run()`)
- **What you found** — one sentence
- **Why it matters** — one sentence explaining the consequence
- **Suggested fix** — brief, concrete

If a category has no findings, omit it entirely. If you find nothing worth reporting, say "No issues found" and stop. Do NOT pad the review with low-value observations.
