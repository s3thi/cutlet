# Rewrite TUTORIAL.md from comments-in-code to prose-with-code-blocks

## Objective

Replace the current `TUTORIAL.md` — a single fenced code block where every explanation is a comment — with a conventional Markdown tutorial that uses normal prose interspersed with fenced code blocks. Done means the tutorial reads like a concise technical document, not like annotated source code.

## Acceptance criteria

- [ ] `TUTORIAL.md` uses Markdown headings, prose paragraphs, and separate fenced `cutlet` code blocks (no single monolithic code block)
- [ ] Every section from the current tutorial is preserved in the same order: Numbers and arithmetic, Strings, Booleans and nothing, Comparison operators, Logical operators, Variables, If/else expressions, While loops, User-defined functions, Anonymous functions, Block scoping, Higher-order functions, Closures, `say()` for output, Running Cutlet programs, The REPL
- [ ] All code examples and their expected outputs are preserved (nothing dropped)
- [ ] Prose is concise — 1-2 sentences per concept, no fluff
- [ ] Comments are only used inside code blocks where they add value (e.g., showing expected output with `# =>` or `# prints:`, or brief inline clarifications); explanations live in prose outside the code blocks
- [ ] The introductory paragraph at the top of the file is preserved or lightly edited
- [ ] Error examples (commented-out lines showing runtime errors) are preserved, either as code or as prose callouts
- [ ] `make test && make check` passes (no source code changes, so this is a documentation-only sanity check — run once at the end to confirm nothing was accidentally broken)

## Constraints

- **Documentation-only change.** No C source files, test files, or Makefiles are modified.
- **Same file path.** The output replaces `TUTORIAL.md` in-place.
- **No new sections or content.** This is a format conversion, not a content expansion. Keep the same concepts and examples.

## Non-goals

- Reorganizing or reordering sections
- Adding new examples or topics
- Changing prose to be verbose or book-like — keep it concise
- Updating code examples for planned but unimplemented features (maps, kebab-case, etc.)

## Dependencies

None. No tasks in `plans/doing/` need to complete first.

## Steps

1. Read current `TUTORIAL.md` to have the full content in context.

2. Rewrite sections 1-8 (Numbers through While loops). For each section:
   - Convert the `# ====` banner into a Markdown `## heading`.
   - Pull comment-only explanations out of the code block into prose paragraphs above/below the relevant code block.
   - Keep `# =>` result annotations and `# prints:` annotations inside code blocks.
   - Keep brief inline comments where they clarify the code (e.g., `# shadows the outer x`).
   - Split the monolithic code block into one code block per logical group of examples.
   - Preserve every code example and every expected-output annotation exactly.

3. Rewrite sections 9-11 (Functions, Anonymous functions, Block scoping) using the same approach as step 2.

4. Rewrite sections 12-14 (Higher-order functions, Closures, `say()`, Running programs, The REPL) using the same approach as step 2. For the REPL section, use a mix of prose and shell code blocks (` ```sh `) for command-line examples.

5. Review the complete file end-to-end:
   - Verify no examples were dropped or altered.
   - Verify the section order matches the original.
   - Verify prose is concise (1-2 sentences per concept).
   - Verify fenced code blocks use the `cutlet` language tag (or `sh` for shell commands).

6. Run `make test && make check` once to confirm nothing was accidentally broken.

## Progress

- [x] Step 1: Read current TUTORIAL.md — read all 1182 lines, 18 sections
- [x] Step 2: Rewrite sections 1-8 (Numbers through While loops) — converted banners to ## headings, extracted comments into prose, split into separate fenced code blocks
