#!/usr/bin/env python3
"""One-file, repository-wide formatter for a C/C++ DAW project.

Usage:
    python3 fmt.py                    # Format all supported, non-ignored files
    python3 fmt.py --check            # Check without modifying files
    python3 fmt.py --check --diff     # Print unified diffs
    python3 fmt.py --changed         # Only Git-modified and untracked files
    python3 fmt.py --staged          # Only paths in Git's staging area
    python3 fmt.py --lang cpp,c,asm  # Restrict languages
    python3 fmt.py src/audio/foo.cpp
    python3 fmt.py --doctor          # Inspect external formatter availability

The script is self-contained (Python standard library only). It invokes specialist
formatters via PATH. C/C++ use clang-format; the other languages require their
corresponding formatter. Assembly and JSON use built-in formatters. Missing
formatters are reported as errors, never silently skipped (including --check).

Exit status: 0 = success, 1 = formatting needed (--check), 2 = error.
"""

from __future__ import annotations

import argparse
import difflib
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parent

# One-file configuration: no .clang-format, .editorconfig or other config is required.
COLUMN_LIMIT = 100
INDENT = 4

# clang-format applies the same K&R style to C, C++, Objective-C and CUDA.
# SeparateDefinitionBlocks adds blank lines between adjacent function definitions.
CLANG_STYLE = (
    "{"
    + ", ".join(
        [
            "BasedOnStyle: LLVM",
            f"IndentWidth: {INDENT}",
            f"TabWidth: {INDENT}",
            "UseTab: Never",
            f"ColumnLimit: {COLUMN_LIMIT}",
            "BreakBeforeBraces: Attach",
            "AllowShortFunctionsOnASingleLine: None",
            "AllowShortBlocksOnASingleLine: Never",
            "AllowShortIfStatementsOnASingleLine: Never",
            "AllowShortLoopsOnASingleLine: false",
            "SeparateDefinitionBlocks: Always",
            "SortIncludes: false",
            "DerivePointerAlignment: false",
            "PointerAlignment: Left",
            "ReferenceAlignment: Left",
            "AccessModifierOffset: -4",
            "IndentCaseLabels: true",
            "NamespaceIndentation: None",
        ]
    )
    + "}"
)

EXCLUDED_DIRS = {
    ".git",
    ".hg",
    ".svn",
    ".idea",
    ".vscode",
    ".cache",
    ".venv",
    "venv",
    "__pycache__",
    "node_modules",
    "target",
    "build",
    "build-debug",
    "build-release",
    "cmake-build-debug",
    "cmake-build-release",
    "_build",
    "_deps",
    "dist",
    "out",
    "vendor",
    "vendors",
    "external",
    "third_party",
    "third-party",
    "generated",
    "_generated",
    ".tox",
    ".mypy_cache",
    ".ruff_cache",
}
EXCLUDED_FILE_NAMES = {
    "CMakeCache.txt",
    "compile_commands.json",
    "package-lock.json",
    "pnpm-lock.yaml",
    "yarn.lock",
    "Cargo.lock",
}
EXCLUDED_GLOBS = ("*.min.js", "*.min.css", "*.generated.*", "*.g.*", "*.pb.*")

C_EXT = {".c"}
CPP_EXT = {
    ".cc",
    ".cpp",
    ".cxx",
    ".c++",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".inl",
    ".tpp",
    ".ixx",
    ".m",
    ".mm",
    ".cu",
    ".cuh",
}
ASM_EXT = {".s", ".asm", ".nasm", ".S"}
PY_EXT = {".py", ".pyi"}
JSON_EXT = {".json"}
YAML_EXT = {".yaml", ".yml"}
MD_EXT = {".md", ".markdown", ".mdx"}
SHELL_EXT = {".sh", ".bash"}
TOML_EXT = {".toml"}
WEB_EXT = {
    ".js",
    ".jsx",
    ".ts",
    ".tsx",
    ".css",
    ".scss",
    ".html",
    ".htm",
    ".vue",
    ".svelte",
    ".jsonc",
}


@dataclass(frozen=True)
class Result:
    path: Path
    status: str  # 'same', 'changed', 'error'
    detail: str = ""
    original: str = ""
    formatted: str = ""
    warning: str = ""


def language(path: Path) -> str | None:
    if path.name == "CMakeLists.txt" or path.suffix.lower() == ".cmake":
        return "cmake"
    # Uppercase .S is preprocessed assembly; lowercase .s is ordinary assembly.
    if path.suffix in ASM_EXT or path.suffix.lower() in {".asm", ".nasm"}:
        return "asm"
    suffix = path.suffix.lower()
    for name, extensions in (
        ("c", C_EXT),
        ("cpp", CPP_EXT),
        ("py", PY_EXT),
        ("json", JSON_EXT),
        ("yaml", YAML_EXT),
        ("md", MD_EXT),
        ("sh", SHELL_EXT),
        ("toml", TOML_EXT),
        ("web", WEB_EXT),
    ):
        if suffix in extensions:
            return name
    return None


def allowed(path: Path, root: Path = ROOT) -> bool:
    try:
        rel = path.relative_to(root)
    except ValueError:
        return False
    if any(part in EXCLUDED_DIRS for part in rel.parts[:-1]):
        return False
    if path.name in EXCLUDED_FILE_NAMES:
        return False
    if any(fnmatch.fnmatch(path.name, glob) for glob in EXCLUDED_GLOBS):
        return False
    return path.is_file() and not path.is_symlink() and language(path) is not None


def git_paths(*arguments: str) -> list[Path] | None:
    try:
        p = subprocess.run(
            ["git", "-C", str(ROOT), *arguments],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return None
    if p.returncode != 0:
        return None
    return [ROOT / Path(os.fsdecode(name)) for name in p.stdout.split(b"\0") if name]


def git_files(mode: str) -> list[Path] | None:
    if mode == "all":
        return git_paths("ls-files", "--cached", "--others", "--exclude-standard", "-z")
    if mode == "staged":
        return git_paths("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z")
    if mode == "changed":
        modifications = git_paths("diff", "HEAD", "--name-only", "--diff-filter=ACMR", "-z")
        untracked = git_paths("ls-files", "--others", "--exclude-standard", "-z")
        if modifications is None:  # No HEAD yet (e.g., new repository).
            modifications = git_paths("ls-files", "--cached", "-z")
        if modifications is None or untracked is None:
            return None
        return modifications + untracked
    raise ValueError(f"Unknown git mode: {mode}")


def walk_files(directory: Path) -> list[Path]:
    found: list[Path] = []
    for here, dirs, files in os.walk(directory, followlinks=False):
        dirs[:] = sorted(
            d for d in dirs if d not in EXCLUDED_DIRS and not (Path(here) / d).is_symlink()
        )
        for name in sorted(files):
            path = Path(here) / name
            if allowed(path):
                found.append(path)
    return found


def choose_paths(paths: list[str], mode: str) -> list[Path]:
    if paths:
        selected: list[Path] = []
        for raw in paths:
            p = Path(raw).expanduser().resolve()
            if not p.exists():
                raise ValueError(f"No such file or directory: {raw}")
            if p.is_dir():
                selected.extend(walk_files(p))
            elif allowed(p):
                selected.append(p)
            else:
                raise ValueError(f"Unsupported or excluded file: {raw}")
    else:
        selected = git_files(mode) if mode != "walk" else None
        if selected is None:
            if mode in {"changed", "staged"}:
                raise ValueError(f"--{mode} requires a Git working tree")
            selected = walk_files(ROOT)
    return sorted({p for p in selected if allowed(p)}, key=lambda p: str(p))


def find_tool(*names: str) -> str | None:
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    return None


def clang_format() -> str | None:
    return find_tool("clang-format", *(f"clang-format-{v}" for v in range(23, 13, -1)))


def invoke(argv: list[str], source: str) -> str:
    try:
        process = subprocess.run(
            argv,
            input=source,
            text=True,
            encoding="utf-8",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except OSError as exc:
        raise RuntimeError(f"Cannot run {Path(argv[0]).name}: {exc}") from exc
    if process.returncode:
        error = (process.stderr or process.stdout).strip()
        raise RuntimeError(f"{Path(argv[0]).name} failed (exit {process.returncode}): {error}")
    return process.stdout


def format_cpp(source: str, path: Path) -> tuple[str, str]:
    tool = clang_format()
    if not tool:
        raise RuntimeError("clang-format is required for C/C++ (install clang-format 14 or newer)")
    return invoke([tool, f"--assume-filename={path}", f"--style={CLANG_STYLE}"], source), ""


def normalize_line_endings(source: str) -> str:
    return source.replace("\r\n", "\n").replace("\r", "\n")


def format_python(source: str, path: Path) -> tuple[str, str]:
    ruff = find_tool("ruff")
    if ruff:
        return invoke(
            [
                ruff,
                "format",
                "--line-length",
                str(COLUMN_LIMIT),
                "--stdin-filename",
                str(path),
                "-",
            ],
            source,
        ), ""
    black = find_tool("black")
    if black:
        return invoke([black, "--quiet", "--line-length", str(COLUMN_LIMIT), "-"], source), ""
    raise RuntimeError("Python formatter not found: install ruff or black")


def format_shell(source: str, path: Path) -> tuple[str, str]:
    tool = find_tool("shfmt")
    if tool:
        return invoke([tool, "-i", str(INDENT), "-ci"], source), ""
    raise RuntimeError("Shell formatter not found: install shfmt")


def format_cmake(source: str, path: Path) -> tuple[str, str]:
    tool = find_tool("gersemi")
    if tool:
        return invoke([tool, "--indent", str(INDENT), "-l", str(COLUMN_LIMIT), "-"], source), ""
    tool = find_tool("cmake-format")
    if tool:
        return invoke([tool, "-"], source), ""
    raise RuntimeError("CMake formatter not found: install gersemi or cmake-format")


def format_prettier(source: str, path: Path, *, markdown: bool = False) -> tuple[str, str]:
    tool = find_tool("prettier")
    if tool:
        cmd = [
            tool,
            "--stdin-filepath",
            str(path),
            "--tab-width",
            "2",
            "--print-width",
            str(COLUMN_LIMIT),
            "--use-tabs",
            "false",
        ]
        if markdown:
            cmd += ["--prose-wrap", "preserve"]
        return invoke(cmd, source), ""
    raise RuntimeError("Prettier not found: install prettier for Markdown/YAML/Web files")


def format_json(source: str, path: Path) -> tuple[str, str]:
    # Validate first. Format the ORIGINAL tokens, not json.dumps(json.loads(...)):
    # that could round a long decimal or normalize string escapes / duplicate keys.
    try:
        json.loads(
            source,
            parse_constant=lambda value: (_ for _ in ()).throw(
                ValueError(f"Non-standard JSON constant: {value}")
            ),
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise RuntimeError(f"Invalid JSON: {exc}") from exc

    src = normalize_line_endings(source)
    out: list[str] = []
    depth = 0
    line_start = True
    previous_token = ""
    i = 0

    def emit(s: str) -> None:
        nonlocal line_start
        if line_start:
            out.append("  " * depth)
            line_start = False
        out.append(s)

    def newline() -> None:
        nonlocal line_start
        if not line_start:
            out.append("\n")
            line_start = True

    while i < len(src):
        char = src[i]
        if char.isspace():
            i += 1
            continue
        if char == '"':
            if previous_token in "{[":
                newline()
            start = i
            i += 1
            escaped = False
            while i < len(src):
                ch = src[i]
                i += 1
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == '"':
                    break
            emit(src[start:i])
            previous_token = '"'
            continue
        if char in "{[":
            if previous_token in "{[":
                newline()
            emit(char)
            depth += 1
            previous_token = char
            i += 1
            continue
        if char in "}]":
            depth -= 1
            if previous_token not in "{[":
                newline()
            emit(char)
            previous_token = char
            i += 1
            continue
        if char == ",":
            emit(char)
            newline()
            previous_token = char
            i += 1
            continue
        if char == ":":
            emit(": ")
            previous_token = char
            i += 1
            continue
        if previous_token in "{[":
            newline()
        emit(char)
        previous_token = char
        i += 1

        # JSON numeric and literal tokens can consist of multiple characters.
        # Treat contiguous non-delimiters as one token for indentation purposes.
        if char not in "{}[],:":
            while i < len(src) and src[i] not in '{}[],:" \r\n\t':
                emit(src[i])
                i += 1
            previous_token = "value"

    return "".join(out).strip("\n") + "\n", ""


_LABEL = re.compile(r"^(?:[A-Za-z_.$?][\w.$?@]*|[0-9]+):(?:\s*(?:[#;].*)?)?$")


def format_asm(source: str, path: Path) -> tuple[str, str]:
    """Light, semantics-conscious assembler formatter (GAS/NASM/AArch64).

    Keep operands, strings, directives and inline comments untouched. Only align
    standalone labels and instruction indentation, and trim line endings. Do not
    rewrite instructions inside C block comments, multiline asm literals or CPP.
    """
    lines = normalize_line_endings(source).split("\n")
    output: list[str] = []
    in_block_comment = False
    in_continuation = False
    for raw in lines:
        text = raw.rstrip("\t ")
        content = text.lstrip(" \t")
        if not content:
            output.append("")
            in_continuation = False
            continue
        if in_block_comment:
            output.append(text)
            if "*/" in text:
                in_block_comment = False
            continue
        if content.startswith("/*") and "*/" not in content:
            in_block_comment = True
            output.append(text)
            continue
        if in_continuation:
            output.append(text)
            in_continuation = text.endswith("\\")
            continue
        # Preprocessor lines, comments and assembler directives retain their body.
        if content.startswith(("#", "%", ";", "//", "*", "@", ".")):
            output.append(content if content.startswith(("#", "%", ".")) else text)
        elif _LABEL.fullmatch(content):
            output.append(content)
        else:
            output.append(" " * INDENT + content)
        in_continuation = text.endswith("\\")
    return "\n".join(output).rstrip("\n") + "\n", ""


def format_toml(source: str, path: Path) -> tuple[str, str]:
    tool = find_tool("taplo")
    if tool:
        return invoke([tool, "fmt", "--stdin-filepath", str(path), "-"], source), ""
    raise RuntimeError("TOML formatter not found: install taplo")


FORMATTERS: dict[str, Callable[[str, Path], tuple[str, str]]] = {
    "c": format_cpp,
    "cpp": format_cpp,
    "asm": format_asm,
    "py": format_python,
    "cmake": format_cmake,
    "json": format_json,
    "yaml": format_prettier,
    "md": lambda s, p: format_prettier(s, p, markdown=True),
    "sh": format_shell,
    "toml": format_toml,
    "web": format_prettier,
}


def format_file(path: Path, check: bool) -> Result:
    try:
        raw = path.read_bytes()
        if b"\x00" in raw:
            return Result(path, "error", "Binary/NUL data in source file")
        has_bom = raw.startswith(b"\xef\xbb\xbf")
        original = raw.decode("utf-8-sig" if has_bom else "utf-8")
        category = language(path)
        assert category is not None
        formatted, warning = FORMATTERS[category](original, path)
        formatted = normalize_line_endings(formatted)
        if formatted and not formatted.endswith("\n"):
            formatted += "\n"
        # Preserve UTF-8 BOM if the source already has one.
        result_bytes = (b"\xef\xbb\xbf" if has_bom else b"") + formatted.encode("utf-8")
        if result_bytes == raw:
            return Result(path, "same", warning=warning)
        if not check:
            # Preserve permissions; write a same-directory temp file then replace.
            import tempfile

            temporary = None
            try:
                with tempfile.NamedTemporaryFile(
                    dir=path.parent, prefix=f".{path.name}.", suffix=".fmt.tmp", delete=False
                ) as tmp:
                    temporary = Path(tmp.name)
                    tmp.write(result_bytes)
                temporary.chmod(path.stat().st_mode)
                os.replace(temporary, path)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        return Result(path, "changed", original=original, formatted=formatted, warning=warning)
    except (OSError, UnicodeError, RuntimeError, ValueError) as exc:
        return Result(path, "error", str(exc))


def doctor() -> int:
    candidates = [
        ("C / C++", clang_format()),
        ("Python", find_tool("ruff", "black")),
        ("Shell", find_tool("shfmt")),
        ("CMake", find_tool("gersemi", "cmake-format")),
        ("Markdown / YAML / Web", find_tool("prettier")),
        ("TOML", find_tool("taplo")),
        ("Assembly / JSON", "built-in"),
    ]
    print("Formatter availability:")
    for category, tool in candidates:
        print(f"  {category:<24} {tool or 'not found (required if language is present)'}")
    print("\nC/C++: embedded K&R clang-format style, 4 spaces, 100 columns, blank lines")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Format C/C++, Assembly and project files from one script."
    )
    parser.add_argument(
        "paths", nargs="*", help="Specific files/directories (default: whole repository)"
    )
    parser.add_argument("--check", action="store_true", help="Check only; never modify files")
    parser.add_argument(
        "--diff", action="store_true", help="Show unified diff for files needing formatting"
    )
    parser.add_argument(
        "--changed", action="store_true", help="Only Git-changed and untracked files"
    )
    parser.add_argument(
        "--staged", action="store_true", help="Only Git-staged paths (working-tree contents)"
    )
    parser.add_argument(
        "--all", action="store_true", help="Format entire tree, even if Git is present"
    )
    parser.add_argument(
        "--lang",
        metavar="LANGS",
        help="Comma-separated language filters (cpp,c,asm,py,cmake,json,yaml,md,sh,toml,web)",
    )
    parser.add_argument(
        "--jobs", type=int, default=1, metavar="N", help="Parallel files (default: 1)"
    )
    parser.add_argument("--doctor", action="store_true", help="Show available formatter tools")
    args = parser.parse_args()
    if args.doctor:
        return doctor()
    if sum((args.changed, args.staged, args.all)) > 1:
        parser.error("--changed, --staged and --all are mutually exclusive")
    if args.jobs < 1:
        parser.error("--jobs must be >= 1")

    kinds = None
    if args.lang:
        kinds = {s.strip().lower() for s in args.lang.split(",") if s.strip()}
        unknown = kinds - FORMATTERS.keys()
        if unknown:
            parser.error("Unknown language(s): " + ", ".join(sorted(unknown)))

    mode = "changed" if args.changed else "staged" if args.staged else "walk" if args.all else "all"
    try:
        targets = choose_paths(args.paths, mode)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2
    if kinds is not None:
        targets = [p for p in targets if language(p) in kinds]
    if not targets:
        print("No supported files found.")
        return 0

    print(f"{'Checking' if args.check else 'Formatting'} {len(targets)} file(s)...")
    if args.jobs == 1:
        results = [format_file(path, args.check) for path in targets]
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda path: format_file(path, args.check), targets))

    count_changed = 0
    count_errors = 0
    warnings: set[str] = set()
    for item in results:
        label = str(item.path.relative_to(ROOT))
        if item.warning:
            warnings.add(item.warning)
        if item.status == "error":
            count_errors += 1
            print(f"[ERROR] {label}: {item.detail}", file=sys.stderr)
        elif item.status == "changed":
            count_changed += 1
            print(f"[{'NEEDS FORMAT' if args.check else 'FORMATTED'}] {label}")
            if args.diff:
                old = item.original.splitlines(keepends=True)
                new = item.formatted.splitlines(keepends=True)
                print(
                    "".join(
                        difflib.unified_diff(old, new, fromfile="a/" + label, tofile="b/" + label)
                    ),
                    end="",
                )
    for warning in sorted(warnings):
        print(f"[WARNING] {warning}", file=sys.stderr)
    print(f"Checked: {len(targets)}, changed: {count_changed}, errors: {count_errors}")
    if count_errors:
        return 2
    if args.check and count_changed:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
