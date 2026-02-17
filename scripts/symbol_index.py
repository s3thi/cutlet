#!/usr/bin/env python3
"""Symbol index generator for the Cutlet codebase.

Uses Universal Ctags to extract all public symbols from src/*.h and produces
a markdown reference document on stdout.

Requires: Universal Ctags (brew install universal-ctags / apt install universal-ctags)
"""

import glob
import json
import os
import shutil
import subprocess
import sys
from collections import defaultdict
from datetime import date


def find_ctags():
    """Find Universal Ctags binary, checking common locations."""
    # Check common Homebrew and system paths for Universal Ctags.
    candidates = [
        "/opt/homebrew/bin/ctags",  # Homebrew on Apple Silicon
        "/usr/local/bin/ctags",     # Homebrew on Intel Mac / Linux
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path

    # Fall back to PATH lookup.
    path = shutil.which("ctags")
    if path:
        return path

    return None


def is_universal_ctags(ctags_bin):
    """Check that the ctags binary is Universal Ctags (not BSD/Exuberant)."""
    try:
        result = subprocess.run(
            [ctags_bin, "--version"],
            capture_output=True, text=True, timeout=5
        )
        return "Universal Ctags" in result.stdout
    except (subprocess.SubprocessError, OSError):
        return False


def run_ctags(ctags_bin, header_files):
    """Run ctags with JSON output on the given header files and return parsed entries."""
    cmd = [
        ctags_bin,
        "--output-format=json",
        "--fields=+Sn",
        "--kinds-c=+fpsgeutd",
        "-f", "-",
    ] + header_files

    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"Error: ctags failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)

    entries = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
            entries.append(entry)
        except json.JSONDecodeError:
            # Skip non-JSON lines (ctags may emit warnings).
            pass

    return entries


def should_skip(entry):
    """Filter out noise: anonymous compiler names, include guards, etc."""
    name = entry.get("name", "")
    kind = entry.get("kind", "")
    # Skip anonymous structs/enums (compiler-generated __anon* names).
    if name.startswith("__anon"):
        return True
    # Skip include guard macros (all-caps ending in _H).
    if kind == "macro" and name.endswith("_H"):
        return True
    return False


# Map ctags kind letters/names to human-readable labels and categories.
TYPE_KINDS = {"struct", "union", "enum", "typedef", "macro"}
FUNC_KINDS = {"function", "prototype"}

KIND_LABELS = {
    "struct": "struct",
    "union": "union",
    "enum": "enum",
    "typedef": "typedef",
    "macro": "macro",
    "function": "function",
    "prototype": "prototype",
    "member": "member",
    "enumerator": "enumerator",
    "externvar": "extern var",
    "variable": "variable",
}


def kind_label(kind_str):
    """Return a human-readable label for a ctags kind."""
    return KIND_LABELS.get(kind_str, kind_str)


def format_markdown(entries_by_file):
    """Format grouped entries as markdown and print to stdout."""
    lines = []
    lines.append("# Symbol Index")
    lines.append("")
    lines.append(f"Generated: {date.today().isoformat()}")
    lines.append("")

    for filepath in sorted(entries_by_file.keys()):
        entries = entries_by_file[filepath]
        lines.append(f"## {filepath}")
        lines.append("")

        # Split into types and functions, filtering out noise.
        types = [e for e in entries if e.get("kind", "") in TYPE_KINDS and not should_skip(e)]
        funcs = [e for e in entries if e.get("kind", "") in FUNC_KINDS and not should_skip(e)]

        if types:
            lines.append("### Types")
            lines.append("| Name | Kind | Line |")
            lines.append("|------|------|------|")
            for e in sorted(types, key=lambda x: x.get("line", 0)):
                name = e.get("name", "?")
                kind = kind_label(e.get("kind", "?"))
                line_num = e.get("line", "?")
                lines.append(f"| {name} | {kind} | {line_num} |")
            lines.append("")

        if funcs:
            lines.append("### Functions")
            lines.append("| Name | Signature | Line |")
            lines.append("|------|-----------|------|")
            for e in sorted(funcs, key=lambda x: x.get("line", 0)):
                name = e.get("name", "?")
                sig = e.get("signature", "")
                # Build a full signature: "return_type name(params)" from
                # the typeref and signature fields if available.
                typeref = e.get("typeref", "")
                if typeref.startswith("typename:"):
                    ret_type = typeref[len("typename:"):]
                else:
                    ret_type = ""
                if ret_type and sig:
                    full_sig = f"`{ret_type} {name}{sig}`"
                elif sig:
                    full_sig = f"`{name}{sig}`"
                else:
                    full_sig = f"`{name}()`"
                line_num = e.get("line", "?")
                lines.append(f"| {name} | {full_sig} | {line_num} |")
            lines.append("")

    return "\n".join(lines)


def main():
    # Find and validate ctags.
    ctags_bin = find_ctags()
    if ctags_bin is None:
        print(
            "Error: Universal Ctags not found.\n"
            "Install it with:\n"
            "  macOS:  brew install universal-ctags\n"
            "  Linux:  apt install universal-ctags",
            file=sys.stderr
        )
        sys.exit(1)

    if not is_universal_ctags(ctags_bin):
        print(
            f"Error: {ctags_bin} is not Universal Ctags (found BSD or Exuberant Ctags).\n"
            "Install Universal Ctags:\n"
            "  macOS:  brew install universal-ctags\n"
            "  Linux:  apt install universal-ctags",
            file=sys.stderr
        )
        sys.exit(1)

    # Find header files (only src/*.h, not vendor/).
    # Resolve paths relative to the project root (parent of scripts/).
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    header_pattern = os.path.join(project_root, "src", "*.h")
    header_files = sorted(glob.glob(header_pattern))

    if not header_files:
        print("Error: no header files found in src/", file=sys.stderr)
        sys.exit(1)

    # Run ctags and parse output.
    entries = run_ctags(ctags_bin, header_files)

    # Group entries by file path (use relative paths for cleaner output).
    entries_by_file = defaultdict(list)
    for entry in entries:
        filepath = entry.get("path", "")
        # Make path relative to project root.
        if filepath.startswith(project_root):
            filepath = os.path.relpath(filepath, project_root)
        entries_by_file[filepath].append(entry)

    # Format and output.
    print(format_markdown(entries_by_file))


if __name__ == "__main__":
    main()
