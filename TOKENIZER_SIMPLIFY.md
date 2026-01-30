# Tokenizer Simplification: Python/Ruby-Style Identifiers + Adjacency

This document specifies the new tokenizer rules and a step-by-step implementation plan.
It is intended as a handoff for another agent.

## Goals

- Simplify identifier rules to match common Python/Ruby behavior (ASCII-only).
- Allow adjacent tokens without whitespace (Python/Ruby style).
- Keep existing minimal feature set (numbers, strings, identifiers, operators).

## New Tokenization Specification

### Character classes (ASCII only)

- **Whitespace**: space, tab, LF, CR.
- **Letter**: `A-Z` or `a-z`.
- **Digit**: `0-9`.
- **Ident start**: letter or `_`.
- **Ident continue**: letter, digit, or `_`.
- **Quote**: `"` (double-quote).
- **Symbol**: any byte that is **not** whitespace, letter, digit, `_`, or `"`.
  - Examples: `+ - * / ( ) { } [ ] , . : ; ! = < > & | ^ ~ ? @ # $ %`.

Non-ASCII bytes are treated as **symbol** for now.

### Tokens

- **IDENT**: `[A-Za-z_][A-Za-z0-9_]*`
  - ASCII only.
  - No Ruby-style prefixes (`@`, `@@`, `$`) or suffixes (`?`, `!`, `=`).
- **NUMBER**: one or more digits.
  - **Error** if the digit run is immediately followed by an ident-start/ident-continue
    character (`[A-Za-z_]`).
  - Examples: `123` ok; `123abc` error; `123_` error.
- **STRING**: double-quoted, no escapes.
  - Consumes characters until the next `"`.
  - Error on newline or EOF before closing quote.
  - **No adjacency error** after the closing quote.
- **OPERATOR**: one or more **symbol** characters.
  - Whitespace delimiting is **not required**.

### Adjacency rules (Python/Ruby style)

- Tokens may be adjacent with no whitespace.
  - Examples: `x+1`, `a=b`, `foo(bar)`, `"a""b"` are all valid token sequences.
- The only adjacency-based error at the tokenizer level is:
  - **NUMBER followed by ident char** (`[A-Za-z_]`) is an error.

## Required Test Updates (exhaustive, tests-first)

Update `tests/test_tokenizer.c` to reflect the new rules:

- **Remove/replace symbol-sandwich identifier tests**:
  - `hello+world`, `kebab-case`, `a-b-c`, `hello_-_world` should become
    IDENT / OPERATOR / IDENT (not a single IDENT).
- **Add identifier tests**:
  - Valid: `_`, `_foo`, `foo_bar`, `foo42`, `Foo`, `__init__`.
  - `_hello` should be valid (currently error).
- **Add adjacency tests (now valid)**:
  - `10+10` => NUMBER, OPERATOR, NUMBER.
  - `-10` => OPERATOR, NUMBER.
  - `"a"+b` => STRING, OPERATOR, IDENT.
  - `foo"bar"` => IDENT, STRING (tokenizer OK).
  - `"a""b"` => STRING, STRING (tokenizer OK).
- **Keep invalid numeric adjacency**:
  - `123abc` should remain an error.
  - `123_` should remain an error.
- **Operator tests without whitespace**:
  - `a+b`, `x==y`, `(foo)` should all tokenize correctly.

## Implementation Steps (must follow AGENTS.md)

1) **Edit tests first** in `tests/test_tokenizer.c` to match the spec above.
2) **Run `make test` and `make check`** before any code changes.
   - Confirm tests fail for the new rules.
   - Pause and ask the user for confirmation before implementing.
3) **Update tokenizer implementation** in `src/tokenizer.c`:
   - Replace symbol-sandwich identifier logic with simple IDENT start/continue rules.
   - Allow `_` as identifier start/continue.
   - Remove whitespace-delimited operator requirement; read symbol runs anywhere.
   - Remove adjacency errors for strings/operators/idents.
   - Keep **number-followed-by-ident-char** as the only adjacency error.
4) **Run `make test` and `make check` after each change** until green.
5) **Do not introduce Unicode handling**; keep ASCII-only rules for now.

## Notes / Non-goals

- No Ruby-style prefixes or suffixes in identifiers.
- No escapes in strings.
- No floating point or negative-number literals (negative numbers remain tokenized
  as `OPERATOR "-"` + `NUMBER`).
- Parser (future) will handle syntax validity beyond token boundaries.
