Read `AGENTS.md` for project conventions, then read `plans/README.md`.

Read every file in `plans/done/` (just the objective/summary, not the full step-by-step) to understand what has already been built. Then read every file in `plans/doing/` to understand what is currently in the work queue.

Read `TUTORIAL.md` and the `examples/` directory to understand the language's current capabilities from a user's perspective.

Now help me figure out what to build next. Guidelines:

- Cutlet's primary purpose is to be a better shell scripting language — a replacement for Bash when scripts outgrow trivial one-liners. Every suggestion should be evaluated through that lens: does it make Cutlet more useful for scripting your system? Features like subprocess execution, pipelines, file/directory manipulation, environment variables, exit code handling, and signal management are all high-value.
- That said, Cutlet also needs to be pleasant to program in, like Python, Ruby, Lua, or JavaScript. Core language ergonomics matter too.
- Suggest 3-5 concrete ideas, ordered by what you think would have the highest impact.
- For each idea, give a one-sentence description and a brief rationale (why now, what it unlocks).
- Factor in what's already queued in `plans/doing/` — don't duplicate those.
- Keep it conversational. This is a brainstorm, not a spec.

After presenting the ideas, ask me which ones interest me (or if I have something else in mind) so we can narrow down. We may go back and forth a few times before settling on something. Once we agree on what to build, tell me to start a new conversation and use `/cutlet-plan` to write the task file.
