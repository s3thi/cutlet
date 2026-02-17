#!/usr/bin/env python3
"""Pipeline tracer for the Cutlet codebase.

Takes a .cutlet file and produces a complete trace through every pipeline
stage (tokens, AST, bytecode) with source location cross-references.

Requires: build/cutlet binary (run `make` first)
          cscope (brew install cscope / apt install cscope)
"""

import os
import re
import subprocess
import sys
from datetime import date


# Reserved words that the parser recognises as keywords (not plain identifiers).
KEYWORDS = frozenset([
    "if", "then", "else", "end", "while", "do", "my",
    "true", "false", "nothing", "and", "or", "not",
    "break", "continue",
])


# ---------------------------------------------------------------------------
# Interpreter output parsing
# ---------------------------------------------------------------------------


def run_interpreter(cutlet_bin, filepath):
    """Run the interpreter with all debug flags and return stdout.

    Runs: cutlet repl --tokens --ast --bytecode < filepath
    Returns stdout as a string.  Exits on failure.
    """
    with open(filepath) as f:
        input_text = f.read()

    result = subprocess.run(
        [cutlet_bin, "repl", "--tokens", "--ast", "--bytecode"],
        input=input_text,
        capture_output=True, text=True, timeout=30,
    )
    if result.returncode != 0:
        print(f"Error: interpreter failed on {filepath}:\n{result.stderr}",
              file=sys.stderr)
        sys.exit(1)
    return result.stdout


def parse_output(raw_output):
    """Parse combined --tokens --ast --bytecode output.

    Returns (token_lines, ast_lines, bytecode_blocks) where each is a list
    of non-empty strings from the respective sections.

    The REPL processes each statement separately.  For each statement it
    emits:
        TOKENS [TYPE value] ...
        AST [...]
        BYTECODE
        == bytecode ==
        <instruction lines>
        <blank line>
        <result value>

    Empty/comment lines produce bare TOKENS/AST/BYTECODE with no payload.
    """
    token_lines = []
    ast_lines = []
    bytecode_blocks = []

    lines = raw_output.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]

        # Collect TOKENS lines that have actual content.
        if line.startswith("TOKENS "):
            token_lines.append(line)
            i += 1
            continue

        # Collect AST lines that have actual content.
        if line.startswith("AST "):
            ast_lines.append(line)
            i += 1
            continue

        # Collect bytecode blocks.  A block starts with "BYTECODE" followed
        # by "== bytecode ==" header and instruction lines until a blank line
        # or non-instruction line.
        if line == "BYTECODE" and i + 1 < len(lines) and lines[i + 1].startswith("== bytecode =="):
            block_lines = []
            i += 2  # skip "BYTECODE" and "== bytecode =="
            while i < len(lines):
                bline = lines[i]
                # Instruction lines start with digits (the address).
                if re.match(r"^\d{4}\s", bline):
                    block_lines.append(bline)
                    i += 1
                else:
                    break
            if block_lines:
                bytecode_blocks.append("\n".join(block_lines))
            continue

        i += 1

    return token_lines, ast_lines, bytecode_blocks


def extract_token_types(token_lines):
    """Extract unique token types from TOKENS lines.

    Token format: [TYPE value]
    Returns sorted list of unique token type strings.
    """
    types = set()
    for line in token_lines:
        for m in re.finditer(r"\[(\w+)\s", line):
            types.add(m.group(1))
    return sorted(types)


def extract_keywords_used(token_lines):
    """Extract keyword identifiers from TOKENS lines.

    Looks for [IDENT word] where word is a reserved keyword.
    Returns sorted list of unique keywords found.
    """
    kws = set()
    for line in token_lines:
        for m in re.finditer(r"\[IDENT\s+(\w+)\]", line):
            word = m.group(1)
            if word in KEYWORDS:
                kws.add(word)
    return sorted(kws)


def extract_ast_node_types(ast_lines):
    """Extract unique AST node type names from AST S-expressions.

    Node types are uppercase words immediately after '[': [NUMBER ...], [BINOP ...].
    Returns sorted list of unique node type names.
    """
    types = set()
    for line in ast_lines:
        for m in re.finditer(r"\[([A-Z_]+)", line):
            types.add(m.group(1))
    return sorted(types)


def extract_opcodes(bytecode_blocks):
    """Extract unique OP_* opcode names from bytecode disassembly.

    Returns sorted list of unique opcode strings.
    """
    opcodes = set()
    for block in bytecode_blocks:
        for m in re.finditer(r"\b(OP_\w+)\b", block):
            opcodes.add(m.group(1))
    return sorted(opcodes)


# ---------------------------------------------------------------------------
# Source location mapping via grep
# ---------------------------------------------------------------------------


def grep_lines(filepath, pattern):
    """Search a file for a regex pattern and return matching (line_number, text) pairs."""
    matches = []
    try:
        with open(filepath) as f:
            for i, line in enumerate(f, 1):
                if re.search(pattern, line):
                    matches.append((i, line.rstrip()))
    except OSError:
        pass
    return matches


def find_keyword_in_parser(keyword, parser_path):
    """Find where the parser recognises a keyword.

    Looks for token_is_keyword(..., "keyword") in parser.c.
    Returns list of (line_number, text).
    """
    pattern = rf'token_is_keyword\(.*"{re.escape(keyword)}"'
    return grep_lines(parser_path, pattern)


def find_ast_type_in_file(ast_type, filepath):
    """Find references to an AST_TYPE in a file.

    Returns list of (line_number, text).
    """
    pattern = rf'\bAST_{re.escape(ast_type)}\b'
    return grep_lines(filepath, pattern)


def find_case_in_file(prefix, name, filepath):
    """Find 'case PREFIX_NAME:' dispatch in a file.

    Returns list of (line_number, text).
    """
    pattern = rf'case\s+{re.escape(prefix)}_{re.escape(name)}\s*:'
    return grep_lines(filepath, pattern)


def find_compile_function(ast_type, compiler_path):
    """Find the compile_* function for an AST type in compiler.c.

    Maps AST_NUMBER -> compile_number, AST_BINOP -> compile_binop, etc.
    Returns (line_number, text) or None.
    """
    func_name = "compile_" + ast_type.lower()
    # Look for the function definition (not just declaration).
    pattern = rf'^static\s+void\s+{re.escape(func_name)}\s*\('
    matches = grep_lines(compiler_path, pattern)
    if matches:
        return matches[0]
    return None


def find_references_in_tests(symbols, test_dir):
    """Find test files that reference any of the given symbols.

    Returns dict mapping test file (relative path) to sorted list of
    matched symbol names.  This gives a per-file summary rather than
    listing every individual line.
    """
    file_symbols = {}  # "tests/test_foo.c" -> set of symbols
    if not os.path.isdir(test_dir):
        return file_symbols
    for fname in sorted(os.listdir(test_dir)):
        if not fname.endswith(".c"):
            continue
        fpath = os.path.join(test_dir, fname)
        relpath = os.path.join("tests", fname)
        try:
            with open(fpath) as f:
                content = f.read()
        except OSError:
            continue
        for sym in symbols:
            if re.search(rf'\b{re.escape(sym)}\b', content):
                file_symbols.setdefault(relpath, set()).add(sym)
    return file_symbols


# ---------------------------------------------------------------------------
# Markdown formatting
# ---------------------------------------------------------------------------


def format_markdown(filepath, source_text, token_lines, ast_lines,
                    bytecode_blocks, token_types, keywords_used,
                    ast_node_types, opcodes, source_locations):
    """Format the full pipeline trace as markdown."""
    lines = []
    basename = os.path.basename(filepath)

    lines.append(f"# Pipeline Trace: {basename}")
    lines.append("")
    lines.append(f"Generated: {date.today().isoformat()}")
    lines.append("")

    # Input program.
    lines.append("## Input Program")
    lines.append("")
    lines.append("```")
    lines.append(source_text.rstrip())
    lines.append("```")
    lines.append("")

    # Token stream.
    lines.append("## Token Stream")
    lines.append("")
    for tl in token_lines:
        # Strip the "TOKENS " prefix for cleaner display.
        lines.append(tl[len("TOKENS "):])
    lines.append("")
    lines.append(f"**Token types used**: {', '.join(token_types)}")
    lines.append("")
    if keywords_used:
        lines.append(f"**Keywords used**: {', '.join(keywords_used)}")
        lines.append("")

    # AST.
    lines.append("## AST")
    lines.append("")
    for al in ast_lines:
        lines.append(al[len("AST "):])
    lines.append("")
    lines.append(f"**Node types used**: {', '.join(ast_node_types)}")
    lines.append("")

    # Bytecode.
    lines.append("## Bytecode Disassembly")
    lines.append("")
    for i, block in enumerate(bytecode_blocks):
        if len(bytecode_blocks) > 1:
            lines.append(f"### Statement {i + 1}")
            lines.append("")
        lines.append("```")
        lines.append(block)
        lines.append("```")
        lines.append("")
    lines.append(f"**Opcodes used**: {', '.join(opcodes)}")
    lines.append("")

    # Source locations.
    lines.append("## Source Locations")
    lines.append("")

    # Parser.
    lines.append("### Parser (`src/parser.c`)")
    lines.append("")
    plocs = source_locations.get("parser", {})
    if plocs:
        for label, entries in sorted(plocs.items()):
            for loc, text in entries:
                lines.append(f"- **{label}**: `src/parser.c:{loc}`")
        lines.append("")
    else:
        lines.append("_(no parser references found)_")
        lines.append("")

    # Compiler.
    lines.append("### Compiler (`src/compiler.c`)")
    lines.append("")
    clocs = source_locations.get("compiler", {})
    if clocs:
        for label, entries in sorted(clocs.items()):
            for loc, text in entries:
                lines.append(f"- **{label}**: `src/compiler.c:{loc}`")
        lines.append("")
    else:
        lines.append("_(no compiler references found)_")
        lines.append("")

    # VM.
    lines.append("### VM (`src/vm.c`)")
    lines.append("")
    vlocs = source_locations.get("vm", {})
    if vlocs:
        for label, entries in sorted(vlocs.items()):
            for loc, text in entries:
                lines.append(f"- **{label}**: `src/vm.c:{loc}`")
        lines.append("")
    else:
        lines.append("_(no VM references found)_")
        lines.append("")

    # Tests.
    lines.append("### Tests")
    lines.append("")
    tlocs = source_locations.get("tests", {})
    if tlocs:
        for test_file in sorted(tlocs.keys()):
            syms = sorted(tlocs[test_file])
            lines.append(f"- `{test_file}`: {', '.join(syms)}")
        lines.append("")
    else:
        lines.append("_(no test references found)_")
        lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file.cutlet>", file=sys.stderr)
        sys.exit(1)

    filepath = sys.argv[1]
    if not os.path.isfile(filepath):
        print(f"Error: file not found: {filepath}", file=sys.stderr)
        sys.exit(1)

    # Resolve project root (parent of scripts/).
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    # Check that the cutlet binary exists.
    cutlet_bin = os.path.join(project_root, "build", "cutlet")
    if not os.path.isfile(cutlet_bin):
        print(
            "Error: build/cutlet not found. Run `make` first.",
            file=sys.stderr
        )
        sys.exit(1)

    # Source paths.
    parser_path = os.path.join(project_root, "src", "parser.c")
    compiler_path = os.path.join(project_root, "src", "compiler.c")
    vm_path = os.path.join(project_root, "src", "vm.c")
    test_dir = os.path.join(project_root, "tests")

    # Read the source program.
    with open(filepath) as f:
        source_text = f.read()

    # Run the interpreter with all debug flags.
    raw_output = run_interpreter(cutlet_bin, filepath)

    # Parse the combined output.
    token_lines, ast_lines, bytecode_blocks = parse_output(raw_output)

    # Handle parse errors: if no tokens/AST/bytecode were captured,
    # note it and exit gracefully.
    if not token_lines and not ast_lines and not bytecode_blocks:
        print(f"# Pipeline Trace: {os.path.basename(filepath)}")
        print()
        print(f"Generated: {date.today().isoformat()}")
        print()
        print("## Input Program")
        print()
        print("```")
        print(source_text.rstrip())
        print("```")
        print()
        print("**Note**: No debug output captured (possible parse error).")
        print()
        print("Interpreter output:")
        print("```")
        print(raw_output.rstrip())
        print("```")
        return

    # Extract summaries.
    token_types = extract_token_types(token_lines)
    keywords_used = extract_keywords_used(token_lines)
    ast_node_types = extract_ast_node_types(ast_lines)
    opcodes = extract_opcodes(bytecode_blocks)

    # Map to source locations.
    source_locations = {
        "parser": {},
        "compiler": {},
        "vm": {},
        "tests": [],
    }

    # Parser: keywords.
    for kw in keywords_used:
        matches = find_keyword_in_parser(kw, parser_path)
        if matches:
            # Take just the first match (the primary recognition site).
            line_num, text = matches[0]
            source_locations["parser"][f'keyword "{kw}"'] = [(line_num, text)]

    # Parser: AST node type creation.
    for node_type in ast_node_types:
        matches = find_ast_type_in_file(node_type, parser_path)
        if matches:
            # Take the first match where the node is created (not just referenced).
            line_num, text = matches[0]
            source_locations["parser"][f"AST_{node_type}"] = [(line_num, text)]

    # Compiler: case dispatch and compile_* functions.
    for node_type in ast_node_types:
        case_matches = find_case_in_file("AST", node_type, compiler_path)
        if case_matches:
            line_num, text = case_matches[0]
            source_locations["compiler"][f"case AST_{node_type}"] = [(line_num, text)]
        func_match = find_compile_function(node_type, compiler_path)
        if func_match:
            line_num, text = func_match
            source_locations["compiler"][f"compile_{node_type.lower()}()"] = [(line_num, text)]

    # VM: opcode dispatch.
    for opcode in opcodes:
        # opcode is like "OP_ADD" — extract the part after "OP_".
        op_name = opcode[3:] if opcode.startswith("OP_") else opcode
        case_matches = find_case_in_file("OP", op_name, vm_path)
        if case_matches:
            line_num, text = case_matches[0]
            source_locations["vm"][f"case {opcode}"] = [(line_num, text)]

    # Tests: find test files that reference the relevant AST types and opcodes.
    all_symbols = [f"AST_{t}" for t in ast_node_types] + opcodes
    source_locations["tests"] = find_references_in_tests(all_symbols, test_dir)

    # Format and print.
    output = format_markdown(
        filepath, source_text, token_lines, ast_lines, bytecode_blocks,
        token_types, keywords_used, ast_node_types, opcodes, source_locations,
    )
    print(output)


if __name__ == "__main__":
    main()
