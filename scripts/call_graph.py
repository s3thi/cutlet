#!/usr/bin/env python3
"""Call graph generator for the Cutlet codebase.

Uses cscope to find callers and callees for every public function defined
in src/*.h, producing a markdown cross-reference on stdout.

Requires: cscope (brew install cscope / apt install cscope)
          Universal Ctags (brew install universal-ctags / apt install universal-ctags)
"""

import glob
import json
import os
import shutil
import subprocess
import sys
from collections import defaultdict
from datetime import date


# --- ctags helpers (shared pattern with symbol_index.py) ---


def find_ctags():
    """Find Universal Ctags binary, checking common locations."""
    candidates = [
        "/opt/homebrew/bin/ctags",  # Homebrew on Apple Silicon
        "/usr/local/bin/ctags",     # Homebrew on Intel Mac / Linux
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
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


def find_cscope():
    """Find cscope binary."""
    path = shutil.which("cscope")
    if path:
        return path
    # Check common Homebrew paths.
    for candidate in ["/opt/homebrew/bin/cscope", "/usr/local/bin/cscope"]:
        if os.path.isfile(candidate):
            return candidate
    return None


def get_public_functions(ctags_bin, header_files):
    """Extract public function names from headers using ctags JSON output."""
    cmd = [
        ctags_bin,
        "--output-format=json",
        "--fields=+Sn",
        "--kinds-c=+fp",  # functions and prototypes only
        "-f", "-",
    ] + header_files

    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"Error: ctags failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)

    functions = {}  # name -> {file, line}
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        name = entry.get("name", "")
        kind = entry.get("kind", "")
        if kind in ("function", "prototype") and not name.startswith("__anon"):
            # Store definition location (prefer .c file later via cscope).
            filepath = entry.get("path", "")
            line_num = entry.get("line", 0)
            # Only store if we haven't seen it, or prefer prototype (header).
            if name not in functions:
                functions[name] = {"file": filepath, "line": line_num}

    return functions


def build_cscope_db(cscope_bin, project_root):
    """Build the cscope database for src/ files only (excludes vendor/)."""
    result = subprocess.run(
        [cscope_bin, "-Rb", "-s", os.path.join(project_root, "src")],
        capture_output=True, text=True, timeout=60,
        cwd=project_root
    )
    if result.returncode != 0:
        print(f"Error: cscope database build failed:\n{result.stderr}",
              file=sys.stderr)
        sys.exit(1)


def cscope_query(cscope_bin, project_root, query_type, symbol):
    """Run a cscope query and return parsed results.

    query_type: 0=find symbol, 1=find definition, 2=find callees,
                3=find callers
    Returns list of dicts: {file, function, line, text}
    """
    result = subprocess.run(
        [cscope_bin, "-d", "-L" + str(query_type), symbol],
        capture_output=True, text=True, timeout=30,
        cwd=project_root
    )
    if result.returncode != 0:
        return []

    results = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        # cscope output format: file function line text
        parts = line.split(None, 3)
        if len(parts) >= 3:
            filepath = parts[0]
            # Normalize absolute paths to be relative to project root.
            if os.path.isabs(filepath):
                try:
                    filepath = os.path.relpath(filepath, project_root)
                except ValueError:
                    pass
            entry = {
                "file": filepath,
                "function": parts[1],
                "line": parts[2],
                "text": parts[3] if len(parts) > 3 else "",
            }
            # Skip vendor files.
            if entry["file"].startswith("vendor/"):
                continue
            results.append(entry)

    return results


def find_definition(cscope_bin, project_root, func_name):
    """Find the definition location of a function using cscope query type 1."""
    results = cscope_query(cscope_bin, project_root, 1, func_name)
    # Prefer src/*.c definitions over header declarations.
    for r in results:
        if r["file"].startswith("src/") and r["file"].endswith(".c"):
            return f"{r['file']}:{r['line']}"
    # Fall back to first result.
    if results:
        return f"{results[0]['file']}:{results[0]['line']}"
    return None


def format_markdown(func_data, project_root):
    """Format the call graph as markdown."""
    lines = []
    lines.append("# Call Graph")
    lines.append("")
    lines.append(f"Generated: {date.today().isoformat()}")
    lines.append("")

    for func_name in sorted(func_data.keys()):
        data = func_data[func_name]
        lines.append(f"## {func_name}")
        lines.append("")

        if data["defined"]:
            lines.append(f"Defined: {data['defined']}")
            lines.append("")

        # Called by (callers).
        # Deduplicate by (file, calling_function) to avoid listing every
        # individual call site for heavily-used functions. Show the first
        # line number where the call occurs in each function.
        callers = data["callers"]
        if callers:
            lines.append("### Called by")
            seen = {}  # (file, function) -> first line
            for c in sorted(callers, key=lambda x: (x["file"], int(x["line"]))):
                key = (c["file"], c["function"])
                if key not in seen:
                    seen[key] = c["line"]
            for (file, func), line_num in sorted(seen.items()):
                lines.append(f"- {file}:{line_num} — {func}()")
            lines.append("")
        else:
            lines.append("### Called by")
            lines.append("_(no callers found)_")
            lines.append("")

        # Calls (callees).
        callees = data["callees"]
        if callees:
            lines.append("### Calls")
            # Deduplicate callee names (cscope may report the same callee
            # multiple times from different call sites).
            seen = set()
            for c in sorted(callees, key=lambda x: x["function"]):
                callee_name = c["function"]
                if callee_name not in seen:
                    seen.add(callee_name)
                    lines.append(f"- {callee_name}")
            lines.append("")
        else:
            lines.append("### Calls")
            lines.append("_(no callees found)_")
            lines.append("")

    return "\n".join(lines)


CSCOPE_FILES = ["cscope.out", "cscope.in.out", "cscope.po.out"]


def cleanup_cscope(project_root):
    """Remove cscope generated files."""
    for f in CSCOPE_FILES:
        path = os.path.join(project_root, f)
        if os.path.exists(path):
            os.remove(path)


def main():
    # Resolve project root (parent of scripts/).
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)

    # Find and validate tools.
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
            f"Error: {ctags_bin} is not Universal Ctags.\n"
            "Install Universal Ctags:\n"
            "  macOS:  brew install universal-ctags\n"
            "  Linux:  apt install universal-ctags",
            file=sys.stderr
        )
        sys.exit(1)

    cscope_bin = find_cscope()
    if cscope_bin is None:
        print(
            "Error: cscope not found.\n"
            "Install it with:\n"
            "  macOS:  brew install cscope\n"
            "  Linux:  apt install cscope",
            file=sys.stderr
        )
        sys.exit(1)

    # Find header files.
    header_pattern = os.path.join(project_root, "src", "*.h")
    header_files = sorted(glob.glob(header_pattern))
    if not header_files:
        print("Error: no header files found in src/", file=sys.stderr)
        sys.exit(1)

    try:
        # Extract public function names from headers.
        functions = get_public_functions(ctags_bin, header_files)
        if not functions:
            print("Error: no public functions found in headers",
                  file=sys.stderr)
            sys.exit(1)

        # Build cscope database.
        build_cscope_db(cscope_bin, project_root)

        # Query callers and callees for each public function.
        func_data = {}
        for func_name in sorted(functions.keys()):
            # Find definition location.
            defined = find_definition(cscope_bin, project_root, func_name)

            # Find callers (query type 3).
            callers = cscope_query(cscope_bin, project_root, 3, func_name)

            # Find callees (query type 2).
            callees = cscope_query(cscope_bin, project_root, 2, func_name)

            func_data[func_name] = {
                "defined": defined,
                "callers": callers,
                "callees": callees,
            }

        # Format and output.
        print(format_markdown(func_data, project_root))

    finally:
        # Always clean up cscope temp files.
        cleanup_cscope(project_root)


if __name__ == "__main__":
    main()
