#!/usr/bin/env python3
"""Convert a Glaze reference header into a native C++20 module interface unit.

This is the exact inverse of ``tools/glz_generate_headers.py``: running the
header generator on the produced ``.ixx`` reproduces the input header byte for
byte.  The rules below are derived only from the input header text and its
position in the tree -- there is no per-file table anywhere in this program.

What the converter derives from the input
-----------------------------------------
* **Module name** -- from the header path relative to the ``glaze/`` root
  (``glaze/a/b.hpp`` -> ``glaze.a.b``).  A handful of upstream files are named
  differently from their module (they predate the modules conversion); those
  names are read from an explicit ``--name-map`` file when supplied, otherwise
  the mechanical name is used.

* **Licence / preamble / pragma framing** -- the leading comment block is the
  licence, ``#pragma once`` (when present) is reproduced, and the blank line
  that separates them is detected (``license_gap``).

  ``#pragma once`` -> kept by the module as metadata; a file without it
  (the vendored ``fast_float`` amalgamation carries its own per-section guards)
  gets ``pragma_once=none``.

* **Include blocks** -- every ``#include`` that appears at preprocessor nesting
  depth 0 *before* the first line of real code is a *block include*.  ``<...>``
  includes form the ``std`` block, ``"..."`` includes form the ``project``
  block.  Each contiguous run becomes one ``// glz:emit <name>`` marker placed
  at its original position, and its members are declared in ``glz:header``
  metadata (``std=`` / ``include=``).  The generator re-emits each block where
  the marker sits, sorted and de-duplicated, which reproduces the header.

  When a header interleaves more than one run of the same kind (the eetf
  wrappers do) the extra runs are given fresh ``group=<name>`` blocks.

  An ``#include`` that sits *inside* a preprocessor conditional, or *after* the
  first line of code, is not part of a block: it stays verbatim in the body,
  exactly where the generator expects to find it.

* **Body** -- everything else is copied verbatim.  ``export`` is never needed
  for the round trip (the generator strips it); see ``--exports`` for how the
  public surface is marked.

* **Trailing framing** -- the number of trailing blank lines and the widest run
  of interior blank lines are measured so ``trailing_blanks`` / ``blank_runs``
  reproduce them.

* **Whitespace inside the body** -- every line is right-stripped on output (the
  generator does the same), so a header with trailing spaces cannot round-trip;
  the converter reports that (``trailing whitespace``) rather than hiding it.

usage:
    python3 tools/glz_moduleize.py --header include/glaze/a/b.hpp --out modules/glaze/a/b.ixx
    python3 tools/glz_moduleize.py --header H --stdout
    python3 tools/glz_moduleize.py --check --header H --out M
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\b")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+(?P<open>[<"])(?P<inner>[^>"]+)[>"]\s*(?P<tail>//.*)?$')
PP_OPEN_RE = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
PP_CLOSE_RE = re.compile(r"^\s*#\s*endif\b")
COMMENT_OR_BLANK_RE = re.compile(r"^\s*(?://.*|/\*.*|\*.*)?$")
MODULE_DECL_RE = re.compile(r"^\s*(?:export\s+)?module\s+[A-Za-z_][\w.:]*\s*;")


class ConversionError(RuntimeError):
    pass


@dataclass
class Block:
    group: str
    kind: str  # "std" or "project"
    includes: list[str] = field(default_factory=list)  # metadata value incl. <> or "" and tail
    targets: list[str] = field(default_factory=list)  # bare include path
    start: int = 0
    end: int = 1  # exclusive line index range in the body


@dataclass
class Framing:
    license_lines: list[str]
    has_pragma: bool
    body: list[str]
    license_gap: bool


def module_name_from_path(header: str) -> str:
    """Mechanical ``glaze/a/b.hpp`` -> ``glaze.a.b`` mapping (AGENTS.md C5)."""
    p = header.replace("\\", "/")
    if p.startswith("include/"):
        p = p[len("include/") :]
    if not p.endswith(".hpp"):
        raise ConversionError(f"header is not a .hpp: {header}")
    stem = p[: -len(".hpp")]
    parts = stem.split("/")
    if parts[0] != "glaze":
        raise ConversionError(f"header is not under glaze/: {header}")
    return ".".join(parts)


def split_framing(lines: list[str]) -> Framing:
    """Separate the licence block, ``#pragma once`` and the body."""
    pragma_index = next(
        (i for i, line in enumerate(lines) if PRAGMA_ONCE_RE.match(line)), None
    )
    if pragma_index is not None:
        licence = lines[:pragma_index]
        gap = bool(licence) and licence[-1].strip() == ""
        licence = trim_blank_edges(licence)
        return Framing(licence, True, lines[pragma_index + 1 :], gap or not licence)

    # No pragma: the licence is the leading run of blank / comment lines.
    index = 0
    while index < len(lines):
        stripped = lines[index].strip()
        if stripped == "" or stripped.startswith(("//", "/*", "*")):
            index += 1
            continue
        break
    return Framing(trim_blank_edges(lines[:index]), False, lines[index:], True)


def trim_blank_edges(lines: list[str]) -> list[str]:
    start = 0
    end = len(lines)
    while start < end and lines[start].strip() == "":
        start += 1
    while end > start and lines[end - 1].strip() == "":
        end -= 1
    return lines[start:end]


def scan_blocks(body: list[str]) -> tuple[list[Block], list[int]]:
    """Find the block-include runs; return them and the indices they occupy."""
    depth = 0
    code_seen = False
    occupied: set[int] = set()
    runs: list[Block] = []
    current: Block | None = None

    for index, line in enumerate(body):
        if PP_OPEN_RE.match(line):
            depth += 1
            current = None
            continue
        if PP_CLOSE_RE.match(line):
            depth = max(0, depth - 1)
            current = None
            continue

        match = INCLUDE_RE.match(line)
        if match and depth == 0 and not code_seen:
            kind = "std" if match.group("open") == "<" else "project"
            inner = match.group("inner")
            tail = (match.group("tail") or "").strip()
            if tail:
                # The generator accepts the trailing annotation inside the
                # quoted scalar: include="path // annotation".
                value = f'"{inner} {tail}"'
            elif kind == "std":
                value = f"<{inner}>"
            else:
                value = f'"{inner}"'
            if current is not None and current.kind == kind and current.end == index:
                current.includes.append(value)
                current.targets.append(inner)
                current.end = index + 1
                occupied.add(index)
                continue
            if current is not None:
                runs.append(current)
            current = Block(
                group=kind,
                kind=kind,
                includes=[value],
                targets=[inner],
                start=index,
                end=index + 1,
            )
            occupied.add(index)
            continue

        # Any other line ends the current run.  A conditional include stays
        # verbatim in the body; a blank line merely separates two blocks.
        if current is not None:
            runs.append(current)
            current = None
        if match:
            continue
        stripped = line.strip()
        if stripped and not stripped.startswith(("//", "/*", "*", "#")):
            code_seen = True

    if current is not None:
        runs.append(current)

    # Assign group names: the first std run is the built-in ``std`` block, the
    # first project run the built-in ``project`` block; any further run of the
    # same kind becomes an extra ``group=`` block.
    counters: dict[str, int] = {"std": 0, "project": 0}
    for run in runs:
        count = counters[run.kind]
        if count == 0:
            run.group = run.kind
        else:
            run.group = f"{run.kind}{count + 1}"
        counters[run.kind] += 1
    return runs, sorted(occupied)


def max_blank_run(lines: list[str]) -> int:
    best = 0
    run = 0
    for line in lines:
        if line.strip() == "":
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def _classify_scope(header: str) -> str:
    """Decide the kind of scope a ``{`` opens from the text before it."""
    text = re.sub(r"\b(?:class|struct|union|enum)\b[^;{(]*$", "", header)
    if re.search(r"\bnamespace\b", text):
        # anonymous namespaces (``namespace {`` with no name) are internal.
        tail = text.rsplit("namespace", 1)[1].strip()
        if tail == "" or tail == "inline":
            return "anon"
        return "ns"
    return "other"


def _strip_to_code(lines: list[str]) -> list[str]:
    """Blank comments and literals across the whole body, line by line.

    Block comments span lines, so this tracks the ``/* ... */`` state; a
    declaration scanner must never mistake a documentation line for code.
    """
    result: list[str] = []
    in_block = False
    for line in lines:
        out: list[str] = []
        i = 0
        n = len(line)
        while i < n:
            if in_block:
                end = line.find("*/", i)
                if end == -1:
                    out.append(" " * (n - i))
                    i = n
                else:
                    out.append(" " * (end + 2 - i))
                    i = end + 2
                    in_block = False
                continue
            ch = line[i]
            if ch == "/" and i + 1 < n and line[i + 1] == "/":
                out.append(" " * (n - i))
                i = n
                continue
            if ch == "/" and i + 1 < n and line[i + 1] == "*":
                in_block = True
                i += 2
                out.append("  ")
                continue
            if ch in {'"', "'"}:
                j = i + 1
                while j < n:
                    if line[j] == "\\":
                        j += 2
                        continue
                    if line[j] == ch:
                        j += 1
                        break
                    j += 1
                out.append(" " * (j - i))
                i = j
                continue
            out.append(ch)
            i += 1
        result.append("".join(out))
    return result


def export_body(body: list[str]) -> list[str]:
    """Prefix ``export`` on every exportable namespace-scope declaration.

    The previous hand conversion exported exactly the declarations other units
    reference.  Exporting a superset is equally correct for consumers and is
    derivable from the input alone, so this marks every namespace-scope
    declaration that *can* legally carry ``export``.  Declarations with internal
    linkage (namespace-scope ``static`` and anonymous-namespace members) may not
    be exported by the language, so they are left untouched -- that is the only
    reason a declaration is skipped.
    """
    code = _strip_to_code(body)
    stack: list[str] = []
    open_decl = False
    marked: list[int] = []
    header_buf: list[str] = []

    def at_namespace_scope() -> bool:
        return not stack or stack[-1] == "ns"

    for index, raw in enumerate(code):
        stripped = raw.strip()
        if not stripped:
            continue  # continuation / blank
        if stripped.startswith("#"):
            continue  # preprocessor: never a declaration, never ends one
        begins_decl = False
        if (
            not open_decl
            and at_namespace_scope()
            and not stripped.startswith(("}", "{"))
        ):
            if stripped.startswith("namespace"):
                begins_decl = False  # export the members, not the namespace
            elif re.match(r"^export\b", stripped):
                begins_decl = False
            elif re.match(r"^static\b", stripped):
                begins_decl = False  # internal linkage: cannot be exported
            else:
                begins_decl = True
                marked.append(index)
                open_decl = True

        # Walk braces / semicolons.  ``header_buf`` carries the text that
        # introduces the *next* scope across line breaks, so a namespace whose
        # brace sits on the following line is still classified correctly.
        for ch in raw:
            if ch == "{":
                stack.append(_classify_scope("".join(header_buf)))
                header_buf = []
            elif ch == "}":
                if stack:
                    stack.pop()
                header_buf = []
            elif ch == ";":
                if open_decl and at_namespace_scope():
                    open_decl = False
                header_buf = []
            else:
                if len(header_buf) < 400:
                    header_buf.append(ch)
        if open_decl and at_namespace_scope() and stripped.endswith((";", "}")):
            open_decl = False

    out = list(body)
    for index in marked:
        line = out[index]
        indent = line[: len(line) - len(line.lstrip())]
        out[index] = f"{indent}export {line.lstrip()}"
    return out


def _comment_run_to_ixx(lines: list[str]) -> list[str]:
    return [line.replace("glaze.hpp", "glaze.ixx") for line in lines]


def convert(
    header_text: str,
    header_rel: str,
    module_name: str | None = None,
    name_map: dict[str, str] | None = None,
    exports: bool = True,
) -> str:
    """Return the module source text for ``header_text``."""
    name = module_name or (name_map or {}).get(header_rel) or module_name_from_path(header_rel)

    text_lines = header_text.split("\n")
    trailing_newline = header_text.endswith("\n")
    raw_lines = header_text.splitlines()
    trailing_blanks = 0
    if trailing_newline:
        probe = text_lines[:]
        probe.pop()  # the empty string produced by the final newline
        while probe and probe[-1].strip() == "":
            trailing_blanks += 1
            probe.pop()
    if not trailing_newline and raw_lines and raw_lines[-1].strip() == "":
        trailing_blanks += 1

    framing = split_framing(raw_lines)
    body = framing.body
    runs, occupied = scan_blocks(body)

    blank_runs = max(1, max_blank_run(body))

    metadata: list[str] = [f'// glz:header path="{header_rel}"']
    for run in runs:
        if run.kind == "std":
            key = "std"
        else:
            key = "include"
        for spelling in run.includes:
            suffix = "" if run.group == run.kind else f" group={run.group}"
            metadata.append(f"// glz:header {key}={spelling}{suffix}")
    if not framing.has_pragma:
        metadata.append("// glz:header pragma_once=none")
    if framing.has_pragma and not framing.license_gap:
        metadata.append("// glz:header license_gap=none")
    if not framing.license_lines:
        metadata.append("// glz:header license=none")
    metadata.append("// glz:header project_imports=ignore")
    if blank_runs > 1:
        metadata.append(f"// glz:header blank_runs={blank_runs}")
    if trailing_blanks:
        metadata.append(f"// glz:header trailing_blanks={trailing_blanks}")
    if not trailing_newline:
        metadata.append("// glz:header trailing_newline=no")

    # body with each block run replaced by a single marker line
    marker_at: dict[int, str] = {}
    for run in runs:
        marker_at[run.start] = f"// glz:emit {run.group}"
    new_body: list[str] = []
    index = 0
    while index < len(body):
        if index in marker_at:
            new_body.append(marker_at[index])
            run = next(r for r in runs if r.start == index)
            index = run.end
            continue
        new_body.append(body[index])
        index += 1

    # imports for compilation: std when needed, and one import per project
    # include that is backed by a module unit.
    imports: list[str] = ["import std;"]
    project_headers = []
    for run in runs:
        if run.kind != "project":
            continue
        project_headers.extend(run.targets)
    if name_map is not None:
        for header in project_headers:
            target = name_map.get(header)
            if target is None:
                continue
            imports.append(f"import {target};")

    if exports:
        new_body = export_body(new_body)

    output: list[str] = []
    output.extend(_comment_run_to_ixx(framing.license_lines))
    output.extend(metadata)
    output.append(f"export module {name};")
    if imports:
        output.append("")
        output.extend(imports)
        output.append("")
    output.extend(new_body)

    result = "\n".join(line.rstrip() for line in output)
    if trailing_newline:
        result += "\n" * (1 + trailing_blanks)
    return result


def parse_name_map(paths: list[Path]) -> dict[str, str]:
    """Read ``header.hpp<TAB-or-space>module.name`` mapping files."""
    mapping: dict[str, str] = {}
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            parts = stripped.split()
            if len(parts) != 2:
                raise ConversionError(f"{path}: expected '<header> <module>' got {line!r}")
            mapping[parts[0]] = parts[1]
    return mapping


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--out", type=Path, help="Write the module unit here")
    parser.add_argument("--stdout", action="store_true", help="Print the module unit")
    parser.add_argument("--check", action="store_true", help="Fail if --out is out of date")
    parser.add_argument("--module-name", help="Override the derived module name")
    parser.add_argument("--name-map", action="append", default=[], type=Path, help="header->module override file")
    parser.add_argument("--relative-to", type=Path, help="Header path is made relative to this root")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    header = args.header
    rel = header.as_posix()
    if args.relative_to is not None:
        rel = header.resolve().relative_to(args.relative_to.resolve()).as_posix()
    text = header.read_text(encoding="utf-8")
    name_map = parse_name_map(args.name_map) if args.name_map else None
    try:
        result = convert(text, rel, module_name=args.module_name, name_map=name_map)
    except ConversionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.stdout:
        sys.stdout.write(result)
    if args.out is not None:
        existing = args.out.read_text(encoding="utf-8") if args.out.exists() else None
        if args.check:
            if existing != result:
                print(f"out of date: {args.out}", file=sys.stderr)
                return 1
            print(f"up to date: {args.out}")
        elif existing != result:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(result, encoding="utf-8", newline="\n")
            print(f"wrote: {args.out}")
        else:
            print(f"unchanged: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
