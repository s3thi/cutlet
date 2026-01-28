# AGENTS.md for Cutlet

Cutlet is a dynamic programming language built entirely using coding agents.

Cutlet is a dynamic programming language similar to Python, Ruby, Lua, and JavaScript. It borrows most heavily from Raku, Perl, and Tcl. It excels at parsing text, navigating files and directories, inter-process communication, job control, and quickly building simple user interfaces for one-off tasks. It's designed to be a glue language that can bring together and orchestrate disparate programs. It's optimized for REPL-driven programming

Cutlet is written in C and has no external dependencies except platform libraries. It only requires a working C23 compiler and a POSIX compliant `make` program. It's designed to run on Linux, macOS, and Windows.

## Important instructions

- Always write tests first. Include a testing strategy in all plans. All code must be exhaustively tested.
- Run `make test` after every code change.
- Comment your code to include guidance and context for future coding agents (and possibly humans).
