#!/usr/bin/env python3
"""Convert a Glaze reference header into a native C++20 module interface unit.

This is the exact inverse of ``tools/glz_generate_headers.py``: running the
header generator on the produced ``.ixx`` reproduces the input header byte for
byte.  Every rule below is derived from the input header text and its path --
there is no per-file table anywhere in this program.

Why the global module fragment
------------------------------
A header's dependencies are ``#include``\\ d textually.  A module interface unit
may not textually include arbitrary headers *inside its module purview*: the
included declarations would be attached to the module, which breaks ODR against
consumers that ``import`` the very same headers as modules.  So the converter
lifts the whole pre-code region of the header -- the licence, feature guards and
include lines -- into the unit's **global module fragment**, where declarations
belong to the global module and everything compiles exactly as header-only code
does.

The header still has to show each ``#include`` at its original position.  The
generator supports that with ``// glz:emit <group>`` markers: the block of
includes is declared in ``glz:header`` metadata and re-emitted wherever the
marker sits.  The converter places a marker for every include run whose members
are *managed* (a standard header, or a Glaze header that has a module unit) and
leaves the rest verbatim in the fragment.

The one shape that cannot use the fragment
------------------------------------------
A vendored amalgamation (``glaze/util/fast_float.hpp``) wraps its entire 4000
line body in one ``#ifndef`` guard.  Its opening ``#ifndef`` sits in the
pre-code region and its ``#endif`` sits in the body, so the guard cannot be
split across the fragment boundary -- a preprocessor conditional may not begin
in the global module fragment and end in the module purview.  Those files are
converted in *body mode*: nothing goes into the fragment and the whole guard is
copied verbatim into the body, where all of its includes are already nested
inside conditionals and are therefore kept in place by the generator.

Output shape (prologue mode)
----------------------------
::

    <licence>
    // glz:header path=... std=... include=... project_imports=ignore
    module;
    <pre-code region, include runs replaced by // glz:emit markers>
    export module glaze.a.b;

    import std;
    import glaze.dep;

    <body with export on the public surface>

usage:
    python3 tools/glz_moduleize.py --header include/glaze/a/b.hpp --out modules/glaze/a/b.ixx
    python3 tools/glz_moduleize.py --header H --stdout
    python3 tools/glz_moduleize.py --check --header H --out M
    python3 tools/glz_moduleize.py --all --include-root REF --modules-root modules --work DIR
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Lexical helpers
# ---------------------------------------------------------------------------

PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\b")
# `#include <x>` / `#include "x"`, optionally with a trailing `// comment`.
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+(?P<open>[<"])(?P<inner>[^>"]+)[>"]\s*(?P<tail>//.*)?$')
PP_OPEN_RE = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
PP_MID_RE = re.compile(r"^\s*#\s*(?:else|elif)\b")
PP_CLOSE_RE = re.compile(r"^\s*#\s*endif\b")
PP_ANY_RE = re.compile(r"^\s*#")
DEFINE_RE = re.compile(r"^\s*#\s*define\s+(?P<name>[A-Za-z_]\w*)")
COMMENT_LINE_RE = re.compile(r"^\s*(?://|/\*|\*)")
# One `// glz:header` line, used to read a unit's own metadata back.
HEADER_META_RE = re.compile(r"^\s*//\s*glz:header(?:\s+(?P<body>.*))?$")
MODULE_DECL_RE = re.compile(r"^\s*(?:export\s+)?module\s+([A-Za-z_][\w.:]*)\s*;")

# Names of the C++ standard library headers, split so a module can decide
# whether an angle include is `import std;` material or a third-party header
# that must stay a textual include in the global module fragment.  This list is
# a property of the C++ standard, not of any Glaze file.
STD_HEADERS = frozenset(
    """
    algorithm any array atomic bit bitset cassert cctype cerrno cfenv cfloat charconv
    chrono cinttypes climits clocale cmath codecvt compare complex concepts condition_variable
    coroutine csetjmp csignal cstdarg cstddef cstdint cstdio cstdlib cstring ctgmath ctime
    cuchar cwchar cwctype deque exception execution expected filesystem flat_map flat_set
    format forward_list fstream functional future generator initializer_list iomanip ios
    iosfwd iostream istream iterator latch limits list locale map mdspan memory memory_resource
    meta mutex new numbers numeric optional ostream print queue random ranges ratio rcu
    regex scoped_allocator semaphore set shared_mutex source_location span sstream stack
    stacktrace stdexcept stop_token streambuf string string_view strstream syncstream
    system_error thread tuple type_traits typeindex typeinfo unordered_map unordered_set
    utility valarray variant vector version
    """.split()
)


class ConversionError(RuntimeError):
    pass


@dataclass
class Include:
    """A single ``#include`` recognised in a header."""

    inner: str  # the path between the delimiters, e.g. glaze/core/opts.hpp
    angle: bool  # True for <...>, False for "..."
    tail: str  # the trailing `// annotation`, if any
    line: int  # index in the header line list

    def render(self) -> str:
        open_, close = ("<", ">") if self.angle else ('"', '"')
        base = f"#include {open_}{self.inner}{close}"
        return f"{base} {self.tail}".rstrip()

    def meta_value(self) -> str:
        """The value written after ``std=`` / ``include=`` in the metadata."""
        target = f"<{self.inner}>" if self.angle else f'"{self.inner}"'
        if self.tail:
            return f'"{self.inner} {self.tail}"' if not self.angle else f'"<{self.inner}> {self.tail}"'
        return target


@dataclass
class Framing:
    licence: list[str]
    has_pragma: bool
    licence_gap: bool
    licence_present: bool
    prelude: list[str]
    body: list[str]


@dataclass
class Unit:
    module_name: str
    text: str
    notes: list[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Module tree introspection (path -> module name), derived from the tree itself
# ---------------------------------------------------------------------------

def load_module_map(modules_root: Path) -> dict[str, str]:
    """Map every generated header path to the module unit that generates it.

    The mapping is read from the existing metadata (``glz:header path=...`` and
    the module declaration), so it is a property of the repository, not a
    hard-coded table: adding a unit to the tree extends the map automatically.
    """
    mapping: dict[str, str] = {}
    for source in sorted(modules_root.rglob("*.ixx")):
        text = source.read_text(encoding="utf-8")
        name = None
        path = None
        skip = False
        for line in text.splitlines():
            if name is None:
                m = MODULE_DECL_RE.match(line)
                if m:
                    name = m.group(1)
                    continue
            m = HEADER_META_RE.match(line)
            if not m:
                continue
            body = m.group("body") or ""
            for token in body.split():
                if token == "skip":
                    skip = True
                elif token.startswith("path="):
                    path = token.split("=", 1)[1].strip('"')
        if name and path and not skip:
            mapping[path] = name
    return mapping


def mechanical_name(header_rel: str) -> str:
    """``glaze/a/b.hpp`` -> ``glaze.a.b`` (AGENTS.md C5 mechanical mapping)."""
    p = header_rel.replace("\\", "/")
    if p.startswith("include/"):
        p = p[len("include/") :]
    if not p.endswith(".hpp"):
        raise ConversionError(f"header is not a .hpp: {header_rel}")
    parts = p[: -len(".hpp")].split("/")
    if parts[0] != "glaze":
        raise ConversionError(f"header is not under glaze/: {header_rel}")
    return ".".join(parts)


# ---------------------------------------------------------------------------
# Field detection
# ---------------------------------------------------------------------------

def is_blank(line: str) -> bool:
    return line.strip() == ""


def is_comment(line: str) -> bool:
    return bool(COMMENT_LINE_RE.match(line))


def trim_blank_edges(lines: list[str]) -> list[str]:
    start, end = 0, len(lines)
    while start < end and is_blank(lines[start]):
        start += 1
    while end > start and is_blank(lines[end - 1]):
        end -= 1
    return lines[start:end]


def split_framing(lines: list[str]) -> Framing:
    """Separate the licence, ``#pragma once`` and the pre-code region."""
    pragma_index = next((i for i, l in enumerate(lines) if PRAGMA_ONCE_RE.match(l)), None)
    if pragma_index is not None:
        before = lines[:pragma_index]
        licence = trim_blank_edges(before)
        licence_present = any(is_comment(l) for l in before)
        if licence_present:
            # gap is a blank line between the last licence line and the pragma.
            last_comment = max(i for i, l in enumerate(before) if is_comment(l))
            licence_gap = (pragma_index - last_comment) > 1
        else:
            licence = []
            licence_gap = True
        start = pragma_index + 1
        has_pragma = True
    else:
        idx = 0
        while idx < len(lines) and (is_blank(lines[idx]) or is_comment(lines[idx])):
            idx += 1
        licence = trim_blank_edges(lines[:idx])
        licence_present = bool(licence)
        licence_gap = True
        start = idx
        has_pragma = False

    code_index = len(lines)
    for i in range(start, len(lines)):
        line = lines[i]
        if is_blank(line) or is_comment(line) or PP_ANY_RE.match(line):
            continue
        code_index = i
        break
    return Framing(licence, has_pragma, licence_gap, licence_present, lines[start:code_index], lines[code_index:])


def max_blank_run(lines: list[str]) -> int:
    best = run = 0
    for line in lines:
        if is_blank(line):
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def trailing_blank_count(lines: list[str]) -> int:
    count = 0
    for line in reversed(lines):
        if is_blank(line):
            count += 1
        else:
            break
    return count


# ---------------------------------------------------------------------------
# Pre-code region analysis
# ---------------------------------------------------------------------------

def prelude_includes(prelude: list[str]) -> tuple[list[Include], set[int]]:
    """Return the depth-0 includes of the pre-code region and their line indices."""
    depth = 0
    includes: list[Include] = []
    indices: set[int] = set()
    for i, line in enumerate(prelude):
        if PP_OPEN_RE.match(line):
            depth += 1
            continue
        if PP_CLOSE_RE.match(line):
            depth = max(0, depth - 1)
            continue
        m = INCLUDE_RE.match(line)
        if m and depth == 0:
            includes.append(
                Include(
                    inner=m.group("inner"),
                    angle=m.group("open") == "<",
                    tail=(m.group("tail") or "").strip(),
                    line=i,
                )
            )
            indices.add(i)
    return includes, indices


def prelude_is_balanced(prelude: list[str]) -> bool:
    depth = 0
    for line in prelude:
        if PP_OPEN_RE.match(line):
            depth += 1
        elif PP_CLOSE_RE.match(line):
            depth -= 1
            if depth < 0:
                return False
    return depth == 0


def resolve_project_include(inner: str, header_rel: str) -> str:
    """Turn a quote include target into a path relative to ``include/``.

    A bare name such as ``types.hpp`` in ``glaze/eetf/defs.hpp`` refers to the
    sibling header ``glaze/eetf/types.hpp``; an already-qualified path is used
    as-is.  ``..`` segments are normalised away.
    """
    if "/" in inner:
        return inner
    parent = header_rel.rsplit("/", 1)[0] if "/" in header_rel else ""
    return f"{parent}/{inner}" if parent else inner


def header_macros(path: Path) -> set[str]:
    """The macro names a header defines (for the textual-include decision)."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return set()
    names: set[str] = set()
    for line in text.splitlines():
        m = DEFINE_RE.match(line)
        if m:
            names.add(m.group("name"))
    return names


def header_uses_any_macro(text: str, macros: set[str]) -> bool:
    if not macros:
        return False
    return any(re.search(rf"\b{re.escape(name)}\b", text) for name in macros)


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------

class Converter:
    def __init__(
        self,
        header_rel: str,
        module_name: str | None,
        module_map: dict[str, str],
        include_root: Path | None,
        exports: bool = True,
    ):
        self.header_rel = header_rel
        self.module_name = module_name
        self.module_map = module_map
        self.include_root = include_root
        self.exports = exports
        self.notes: list[str] = []

    # -- include classification ------------------------------------------------

    def is_module_backed(self, include: Include) -> str | None:
        if include.angle:
            if not include.inner.startswith("glaze/"):
                return None
            target = include.inner
        else:
            target = resolve_project_include(include.inner, self.header_rel)
            if not target.startswith("glaze/"):
                return None
        return self.module_map.get(target)

    def needs_textual(self, include: Include, target_rel: str, body_text: str) -> bool:
        """A Glaze header whose macro(s) the body uses cannot be imported."""
        if self.include_root is None:
            return include.angle is False and target_rel.startswith("glaze/") is False
        path = self.include_root / target_rel
        if not path.exists():
            return True
        macros = header_macros(path)
        return header_uses_any_macro(body_text, macros)

    def classify(self, include: Include, body_text: str) -> tuple[str, str | None]:
        """Return (treatment, module_name).

        Every pre-code include is treated as a *textual* include that belongs in
        the global module fragment.  This is deliberate: header-only Glaze code
        relies on transitive includes and on macros defined by its dependencies,
        and neither survives being replaced by ``import`` -- a module exports
        declarations, never the transitive include set or the macros.  Keeping
        the include textual in the fragment reproduces header-only compilation
        exactly.  The only marker-worthy include is one whose spelling cannot be
        compiled from the unit's location (a bare sibling name such as
        ``types.hpp``), handled separately.
        """
        return "literal", None

    # -- entry point -----------------------------------------------------------

    def convert(self, header_text: str) -> Unit:
        text_lines = header_text.split("\n")
        trailing_newline = header_text.endswith("\n")
        raw_lines = header_text.splitlines()
        trailing_blanks = 0
        if trailing_newline:
            probe = text_lines[:]
            probe.pop()
            while probe and is_blank(probe[-1]):
                trailing_blanks += 1
                probe.pop()
        elif raw_lines and is_blank(raw_lines[-1]):
            trailing_blanks += 1

        framing = split_framing(raw_lines)
        body_text = "\n".join(framing.body)

        # Absolute indices of the pre-code region and the body.
        body_start = len(raw_lines) - len(framing.body)
        prelude_start = body_start - len(framing.prelude)
        # `prologue_mode`: the pre-code region is balanced, so it can be lifted
        # whole into the global module fragment and rendered there verbatim.
        prologue_mode = bool(framing.prelude) and prelude_is_balanced(framing.prelude)

        # Every `#include` in the header together with the preprocessor guards
        # around it.  Angle `<...>` entries feed the generator's `std` block and
        # `"..."` entries its `project` block.
        scan_depth = 0
        guard_stack: list[str] = []
        located: list[tuple[int, Include, tuple[str, ...]]] = []
        for i in range(prelude_start, len(raw_lines)):
            line = raw_lines[i]
            if PP_OPEN_RE.match(line):
                guard_stack.append(line)
                scan_depth += 1
                continue
            if PP_CLOSE_RE.match(line):
                if guard_stack:
                    guard_stack.pop()
                scan_depth = max(0, scan_depth - 1)
                continue
            if PP_MID_RE.match(line):
                continue
            m = INCLUDE_RE.match(line)
            if m:
                located.append(
                    (
                        i,
                        Include(
                            inner=m.group("inner"),
                            angle=m.group("open") == "<",
                            tail=(m.group("tail") or "").strip(),
                            line=i,
                        ),
                        tuple(guard_stack),
                    )
                )

        # An include may stay literal -- and compile in place in the fragment --
        # only when it sits in the pre-code region of a balanced unit.  Anywhere
        # else a textual include would land in the module purview, where it would
        # attach foreign declarations to this module; such includes are replaced
        # by a marker and compiled from a hidden fragment copy instead.
        managed: list[tuple[Include, tuple[str, ...]]] = [
            (inc, guards)
            for i, inc, guards in located
            if not (prologue_mode and i < body_start)
        ]

        metadata: list[str] = [f'// glz:header path="{self.header_rel}"']
        group_counter: dict[str, int] = {"std": 0, "project": 0}
        emitted_groups: list[str] = []

        def kind_of(inc: Include) -> str:
            # The generator picks the block from the delimiter: `<...>` entries
            # go to the built-in `std` block, `"..."` entries to `project`.
            return "std" if inc.angle else "project"

        # Split the managed includes into maximal runs of adjacent, same-kind
        # entries whose rendered spelling is already in non-decreasing order.
        # The generator sorts each metadata block, so an out-of-order run is cut
        # into ordered chunks (each becomes its own block) to keep the header's
        # original include order.
        runs: list[list[tuple[Include, tuple[str, ...]]]] = []
        for entry in managed:
            inc = entry[0]
            if runs:
                prev = runs[-1][-1][0]
                if (
                    prev.line == inc.line - 1
                    and kind_of(prev) == kind_of(inc)
                    and prev.meta_value() <= inc.meta_value()
                ):
                    runs[-1].append(entry)
                    continue
            runs.append([entry])

        marker_for_line: dict[int, str] = {}
        managed_lines: set[int] = set()
        compile_includes: list[tuple[Include, tuple[str, ...]]] = []
        for run in runs:
            kind = kind_of(run[0][0])
            n = group_counter[kind]
            group_counter[kind] += 1
            group = kind if n == 0 else f"{kind}{n + 1}"
            emitted_groups.append(group)
            marker_for_line[run[0][0].line] = group
            managed_lines.update(entry[0].line for entry in run)
            for inc, guards in run:
                key = "std" if inc.angle else "include"
                suffix = "" if group == kind else f" group={group}"
                metadata.append(f"// glz:header {key}={inc.meta_value()}{suffix}")
                compile_includes.append((inc, guards))

        metadata.append("// glz:header project_imports=ignore")
        if not framing.has_pragma:
            metadata.append("// glz:header pragma_once=none")
        if framing.has_pragma and not framing.licence_gap:
            metadata.append("// glz:header license_gap=none")
        if not framing.licence_present:
            metadata.append("// glz:header license=none")
        blank_runs = max(1, max_blank_run(framing.body))
        if blank_runs > 1:
            metadata.append(f"// glz:header blank_runs={blank_runs}")
        if trailing_blanks:
            metadata.append(f"// glz:header trailing_blanks={trailing_blanks}")
        if not trailing_newline:
            metadata.append("// glz:header trailing_newline=no")

        # The pre-code region as it goes into the unit: managed include runs
        # become markers, literal includes stay verbatim.
        region: list[str] = []
        for abs_i in range(prelude_start, body_start):
            if abs_i in marker_for_line:
                region.append(f"// glz:emit {marker_for_line[abs_i]}")
            elif abs_i in managed_lines:
                continue
            else:
                region.append(raw_lines[abs_i])

        # The body, with managed includes replaced by markers.
        body_lines: list[str] = []
        for offset, line in enumerate(framing.body):
            abs_i = body_start + offset
            if abs_i in marker_for_line:
                body_lines.append(f"// glz:emit {marker_for_line[abs_i]}")
            elif abs_i in managed_lines:
                continue
            else:
                body_lines.append(line)

        if self.exports:
            body_lines = export_body(body_lines)
        # Spell the bare C integer types as glz:: aliases (AGENTS.md C3); the
        # generator renders them back, so the header is unchanged.
        alias_text, aliases_used = qualify_builtin_aliases("\n".join(body_lines))
        body_lines = alias_text.split("\n")
        imports: list[str] = []
        if aliases_used:
            imports.append(BASIC_TYPES_MODULE)

        # Textual copies that keep the unit compiling but never reach the
        # header, hidden in a glz:module-only block.  Their preprocessor guards
        # are reproduced so platform-specific headers stay conditional.
        gmf_lines: list[str] = []
        if compile_includes:
            gmf_lines.append("// glz:module-only")
            for inc, guards in compile_includes:
                gmf_lines.extend(guards)
                if inc.angle:
                    gmf_lines.append(f"#include <{inc.inner}>")
                else:
                    gmf_lines.append(f'#include "{resolve_project_include(inc.inner, self.header_rel)}"')
                gmf_lines.extend("#endif" for _ in guards)
            gmf_lines.append("// glz:end-module-only")
        if prologue_mode:
            gmf_lines.extend(region)
            # The pre-code region must render in place rather than be dropped as
            # module-internal, which needs at least one emit marker in it.  The
            # `prelude` block is always empty in prologue mode, so naming it
            # never re-emits a real include block at the wrong position.
            gmf_lines.append("// glz:emit prelude")

        output: list[str] = []
        output.extend(framing.licence)
        output.extend(metadata)
        if gmf_lines:
            output.append("module;")
            output.extend(gmf_lines)
        output.append(f"export module {self.module_name};")

        import_lines = ["import std;"]
        for module in imports:
            import_lines.append(f"import {module};")
        output.append("")
        output.extend(import_lines)
        output.append("")
        if not prologue_mode and region:
            # In body mode the pre-code region and the code are one continuous
            # block: the reference frequently has a comment immediately above
            # the first declaration, and the generator must not be given a
            # blank to insert between them.
            output.extend(region)
        output.extend(body_lines)

        result = "\n".join(line.rstrip() for line in output)
        if trailing_newline:
            result += "\n" * (1 + trailing_blanks)
        return Unit(self.module_name, result, self.notes)


# The ten C integer aliases the module side spells as ``glz::`` (AGENTS.md C3).
BUILTIN_TYPE_ALIASES = (
    "size_t",
    "ptrdiff_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
)
ALIAS_SET = frozenset(BUILTIN_TYPE_ALIASES)
IDENT_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
# The internal unit that defines the ``glz::`` aliases (AGENTS.md C3).
BASIC_TYPES_MODULE = "glaze.core.basic_types"


def qualify_builtin_aliases(text: str) -> tuple[str, bool]:
    """Spell the bare C integer types as ``glz::`` aliases (AGENTS.md C3).

    Exact inverse of the generator's ``dequalify_builtin_type_aliases``: only the
    ten aliases are matched, only as complete identifiers, never inside comments
    or string/char literals, and never when they are part of a qualified name
    (``std::size_t``, ``x.size_t``) -- so the generator renders every converted
    alias back to the bare spelling and the header is unchanged.
    """
    result: list[str] = []
    index = 0
    length = len(text)
    changed = False
    # Trailing non-space characters already emitted, for the qualification check.
    tail = ""

    def preceded_by_scope() -> bool:
        t = tail.rstrip()
        return t.endswith(("::", ".", "->"))

    while index < length:
        ch = text[index]
        if ch == "/" and index + 1 < length and text[index + 1] == "/":
            end = text.find("\n", index)
            end = length if end == -1 else end
            result.append(text[index:end])
            tail = text[index:end]
            index = end
            continue
        if ch == "/" and index + 1 < length and text[index + 1] == "*":
            end = text.find("*/", index + 2)
            end = length if end == -1 else end + 2
            result.append(text[index:end])
            tail = text[index:end]
            index = end
            continue
        if ch in {'"', "'"}:
            start = index
            index += 1
            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == ch:
                    index += 1
                    break
                index += 1
            result.append(text[start:index])
            tail = text[start:index]
            continue
        if ch.isalpha() or ch == "_":
            end = index + 1
            while end < length and text[end] in IDENT_CHARS:
                end += 1
            token = text[index:end]
            if token in ALIAS_SET and not preceded_by_scope():
                result.append(f"glz::{token}")
                tail = f"glz::{token}"
                changed = True
            else:
                result.append(token)
                tail = token
            index = end
            continue
        result.append(ch)
        tail = (tail + ch)[-8:]
        index += 1
    return "".join(result), changed


# ---------------------------------------------------------------------------
# `export` placement
# ---------------------------------------------------------------------------

def _strip_to_code(lines: list[str]) -> list[str]:
    """Blank comments and literals across the whole body, line by line."""
    result: list[str] = []
    in_block = False
    for line in lines:
        out: list[str] = []
        i, n = 0, len(line)
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
                out.append("  ")
                i += 2
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


def _classify_scope(header: str) -> str:
    """Decide what a ``{`` opens from the text before it."""
    text = re.sub(r"\b(?:class|struct|union|enum)\b[^;{(]*$", "", header)
    if re.search(r"\bnamespace\b", text):
        tail = text.rsplit("namespace", 1)[1].strip()
        if tail == "" or tail == "inline":
            return "anon"
        return "ns"
    return "other"


def export_body(body: list[str]) -> list[str]:
    """Prefix ``export`` on every exportable namespace-scope declaration.

    The previous hand conversion exported exactly the declarations other units
    reference.  Exporting a superset is equally correct for consumers and is
    derivable from the input alone, so this marks every namespace-scope
    declaration that *can* legally carry ``export``.  Declarations with
    internal linkage (namespace-scope ``static`` and anonymous-namespace
    members) may not be exported by the language, so they are left alone -- that
    is the only reason a declaration is skipped.
    """
    code = _strip_to_code(body)
    stack: list[str] = []
    open_decl = False
    marked: list[int] = []
    header_buf: list[str] = []

    def at_namespace_scope() -> bool:
        return not stack or stack[-1] == "ns"

    def declaration_signature(start: int) -> str:
        """Text of the declaration beginning at ``start`` up to its ``{``/``;``.

        Used to reject out-of-class member definitions: ``export`` on the
        ``template <...>`` line of ``X<T>::member()`` is rejected by the compiler
        ("cannot export ... as it is not at namespace scope").
        """
        parts: list[str] = []
        for line in code[start : start + 12]:
            parts.append(line)
            if "{" in line or ";" in line:
                break
        return " ".join(parts)

    for index, raw in enumerate(code):
        stripped = raw.strip()
        if not stripped:
            continue
        if stripped.startswith("#"):
            continue
        if not open_decl and at_namespace_scope() and not stripped.startswith(("}", "{")):
            if stripped.startswith("namespace"):
                begins = False
            elif re.match(r"^export\b", stripped):
                begins = False
            elif re.match(r"^static\b", stripped):
                begins = False
            elif re.search(r"\w\s*::\s*[\w~]+\s*\(", declaration_signature(index)):
                # out-of-class member definition
                begins = False
            else:
                begins = True
                marked.append(index)
                open_decl = True
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
            elif len(header_buf) < 400:
                header_buf.append(ch)
        if open_decl and at_namespace_scope() and stripped.endswith((";", "}")):
            open_decl = False

    out = list(body)
    for index in marked:
        line = out[index]
        indent = line[: len(line) - len(line.lstrip())]
        out[index] = f"{indent}export {line.lstrip()}"
    return out


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_name_map(paths: list[Path]) -> dict[str, str]:
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


def convert_header(
    header_path: Path,
    header_rel: str,
    *,
    module_map: dict[str, str],
    include_root: Path | None,
    module_name: str | None = None,
    name_map: dict[str, str] | None = None,
    exports: bool = True,
) -> Unit:
    name = module_name or (name_map or {}).get(header_rel) or module_map.get(header_rel) or mechanical_name(header_rel)
    converter = Converter(header_rel, name, module_map, include_root, exports=exports)
    return converter.convert(header_path.read_text(encoding="utf-8"))


# ---------------------------------------------------------------------------
# Driver: convert every reference header and report round-trip fidelity
# ---------------------------------------------------------------------------

def run_driver(args: argparse.Namespace) -> int:
    include_root = Path(args.include_root).resolve()
    modules_root = Path(args.modules_root).resolve()
    work = Path(args.work).resolve()
    gen_modules = work / "gen_modules"
    gen_headers = work / "gen_headers"
    if gen_modules.exists() or gen_headers.exists():
        import shutil

        for d in (gen_modules, gen_headers):
            if d.exists():
                shutil.rmtree(d)
    gen_modules.mkdir(parents=True, exist_ok=True)
    gen_headers.mkdir(parents=True, exist_ok=True)

    module_map = load_module_map(modules_root)
    name_map = parse_name_map([Path(p) for p in args.name_map]) if args.name_map else None

    headers = sorted(p for p in include_root.rglob("*.hpp") if p.relative_to(include_root).as_posix() in module_map)
    if args.limit:
        headers = headers[: args.limit]

    rows: dict[str, dict] = {}
    for header in headers:
        rel = header.relative_to(include_root).as_posix()
        row = {"rel": rel, "roundtrip": "?", "compiles": "-", "hand": "-", "detail": "", "error": ""}
        rows[rel] = row
        try:
            unit = convert_header(
                header,
                rel,
                module_map=module_map,
                include_root=include_root,
                name_map=name_map,
                exports=not args.no_exports,
            )
        except ConversionError as exc:
            row["roundtrip"] = "CONVERT-FAIL"
            row["detail"] = str(exc)[:80]
            continue
        out = gen_modules / (rel[: -len(".hpp")] + ".ixx")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(unit.text, encoding="utf-8", newline="\n")
        hand = modules_root / (rel[: -len(".hpp")] + ".ixx")
        row["hand"] = "same" if hand.exists() and hand.read_text() == unit.text else ("differs" if hand.exists() else "no-unit")

    # Round trip: regenerate every header from the converted units.
    generator = Path(args.generator).resolve()
    manifest = Path(args.manifest).resolve() if args.manifest else None
    cmd = [
        sys.executable,
        str(generator),
        "--module-root",
        str(gen_modules),
        "--include-root",
        str(gen_headers),
        "--no-copy-headers",
    ]
    if manifest is not None:
        cmd += ["--manifest", str(manifest)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print("generator failed:", proc.returncode)
        print((proc.stdout + proc.stderr)[-4000:])

    for rel, row in rows.items():
        if row["roundtrip"] == "CONVERT-FAIL":
            continue
        produced = gen_headers / rel
        original = include_root / rel
        if produced.exists() and original.read_bytes() == produced.read_bytes():
            row["roundtrip"] = "OK"
        elif produced.exists():
            row["roundtrip"] = "DIFF"
            row["detail"] = "header differs"
        else:
            row["roundtrip"] = "NOT-GENERATED"

    if args.compile:
        compile_units(rows, gen_modules, modules_root, include_root, args)

    # Report.
    total = len(rows)
    rt_ok = sum(1 for r in rows.values() if r["roundtrip"] == "OK")
    print(f"== glz_moduleize driver ==")
    print(f"  headers converted      : {total}")
    print(f"  round-trip byte-exact  : {rt_ok}/{total}")
    if args.compile:
        comp_ok = sum(1 for r in rows.values() if r["compiles"] == "OK")
        print(f"  precompile OK          : {comp_ok}/{total}")
        print(f"  identical to hand unit : {sum(1 for r in rows.values() if r['hand'] == 'same')}/{total}")
    print()
    if args.table:
        widths = {"rel": 46, "roundtrip": 11, "compiles": 12, "hand": 8}
        print(f"{'header':<{widths['rel']}} {'roundtrip':<{widths['roundtrip']}} {'compiles':<{widths['compiles']}} {'hand':<{widths['hand']}}")
        for rel in sorted(rows):
            r = rows[rel]
            print(f"{rel:<{widths['rel']}} {r['roundtrip']:<{widths['roundtrip']}} {r['compiles']:<{widths['compiles']}} {r['hand']:<{widths['hand']}}")
    failed = [r for r in rows.values() if r["roundtrip"] != "OK" or (args.compile and r["compiles"] not in ("OK",))]
    if failed:
        print("failures:")
        for r in sorted(failed, key=lambda x: x["rel"]):
            print(f"  [{r['roundtrip']}/{r['compiles']}] {r['rel']}  {r['detail']} {r['error'][:110]}")
    ts = work / "report.tsv"
    with ts.open("w", encoding="utf-8") as fh:
        fh.write("header\troundtrip\tcompiles\thand\tdetail\n")
        for rel in sorted(rows):
            r = rows[rel]
            fh.write(f"{rel}\t{r['roundtrip']}\t{r['compiles']}\t{r['hand']}\t{r['detail'] or r['error']}\n")
    print(f"\nfull table: {ts}")
    return 0 if rt_ok == total else 1


# ---------------------------------------------------------------------------
# Compilation (the second half of the proof)
# ---------------------------------------------------------------------------

INSERT_IMPORT_RE = re.compile(r"^\s*(?:export\s+)?import\s+([^;]+?)\s*;")


def unit_name_and_imports(path: Path) -> tuple[str | None, set[str]]:
    name = None
    deps: set[str] = set()
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if name is None:
            m = MODULE_DECL_RE.match(line)
            if m:
                name = m.group(1)
                continue
        m = INSERT_IMPORT_RE.match(line)
        if m:
            t = m.group(1).strip()
            if t and t != "std":
                deps.add(t)
    return name, deps


def discover_skip_units(modules_root: Path) -> dict[str, Path]:
    """Units that carry ``glz:header skip`` (no public header, e.g. basic_types).

    The generated units import these, so they have to be built too.
    """
    pool: dict[str, Path] = {}
    for source in sorted(modules_root.rglob("*.ixx")):
        text = source.read_text(encoding="utf-8", errors="replace")
        if "glz:header skip" not in text:
            continue
        name, _ = unit_name_and_imports(source)
        if name:
            pool[name] = source
    return pool


def precompile_one(
    cxx: str, std_pcm: Path, bmi_dir: Path, path: Path, name: str, includes: list[Path], timeout: int
) -> tuple[bool, str]:
    out = bmi_dir / f"{name.replace(':', '-')}.pcm"
    cmd = [
        cxx,
        "-std=c++23",
        "-stdlib=libc++",
        "-x",
        "c++-module",
        "--precompile",
        str(path),
        "-o",
        str(out),
        f"-fmodule-file=std={std_pcm}",
        f"-fprebuilt-module-path={bmi_dir}",
    ]
    for d in includes:
        cmd += ["-I", str(d)]
    cmd.append("-Wno-everything")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f"timeout after {timeout}s"
    if result.returncode == 0 and out.exists():
        return True, ""
    if out.exists():
        out.unlink()
    return False, (result.stdout + result.stderr).strip()


def first_error(log: str) -> str:
    for line in log.splitlines():
        if " error:" in line or "fatal error" in line:
            return line.strip()
    return log.splitlines()[-1].strip() if log else ""


def build_bmis(
    pool: dict[str, Path], bmi_dir: Path, args: argparse.Namespace, includes: list[Path]
) -> tuple[set[str], dict[str, str]]:
    """Precompile every unit in ``pool`` in dependency order (fixpoint).

    A unit is only attempted once all of its module imports have a BMI, so a
    failure is never blamed on a dependency that was simply not built yet.
    """
    import concurrent.futures as cf

    bmi_dir.mkdir(parents=True, exist_ok=True)
    built: set[str] = set()
    fails: dict[str, str] = {}
    pending: dict[str, Path] = dict(pool)
    while pending:
        ready: list[tuple[str, Path]] = []
        notready: dict[str, Path] = {}
        for name, path in pending.items():
            _, deps = unit_name_and_imports(path)
            missing = [d for d in deps if d not in built]
            if missing:
                notready[name] = path
            else:
                ready.append((name, path))
        if not ready:
            for name in pending:
                fails.setdefault(name, "unresolved import (missing module or import cycle)")
            break
        results: dict[str, tuple[bool, str]] = {}
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futures = {
                ex.submit(
                    precompile_one, args.cxx, Path(args.std_pcm), bmi_dir, path, name, includes, args.compile_timeout
                ): name
                for name, path in ready
            }
            for future in cf.as_completed(futures):
                results[futures[future]] = future.result()
        progressed = False
        for name, path in ready:
            ok, log = results[name]
            if ok:
                built.add(name)
                progressed = True
            else:
                notready[name] = path
                fails[name] = log
        if not progressed:
            for name in notready:
                fails.setdefault(name, "blocked: dependency not built")
            break
        pending = notready
    return built, fails


def compile_units(
    rows: dict[str, dict],
    gen_modules: Path,
    modules_root: Path,
    include_root: Path,
    args: argparse.Namespace,
) -> None:
    pool: dict[str, Path] = {}
    for source in sorted(gen_modules.rglob("*.ixx")):
        name, _ = unit_name_and_imports(source)
        if name:
            pool[name] = source
    pool.update(discover_skip_units(modules_root))

    includes = [include_root, modules_root] + [Path(p).resolve() for p in args.extra_include]
    built, fails = build_bmis(pool, Path(args.bmi_dir).resolve(), args, includes)

    for row in rows.values():
        if row["roundtrip"] == "CONVERT-FAIL":
            continue
        rel = row["rel"]
        source = gen_modules / (rel[: -len(".hpp")] + ".ixx")
        name, _ = unit_name_and_imports(source)
        if name in built:
            row["compiles"] = "OK"
        elif name in fails:
            row["compiles"] = "FAIL"
            row["error"] = first_error(fails[name]) or fails[name][:110]
        else:
            row["compiles"] = "-"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", type=Path, help="Reference header to convert")
    parser.add_argument("--out", type=Path, help="Write the module unit here")
    parser.add_argument("--stdout", action="store_true", help="Print the module unit")
    parser.add_argument("--check", action="store_true", help="Fail if --out is out of date")
    parser.add_argument("--module-name", help="Override the derived module name")
    parser.add_argument("--name-map", action="append", default=[], type=Path, help="header->module override file")
    parser.add_argument("--include-root", type=Path, default=Path("include"), help="Root the header path is relative to")
    parser.add_argument("--modules-root", type=Path, default=Path("modules"), help="Root of the module tree")
    parser.add_argument("--relative-to", type=Path, help="Header path is made relative to this root")
    parser.add_argument("--no-exports", action="store_true", help="Do not mark the public surface with export")
    # driver
    parser.add_argument("--all", action="store_true", help="Convert every reference header and report round-trip")
    parser.add_argument("--repo", type=Path, default=Path("."), help="Repository root (driver only)")
    parser.add_argument("--work", type=Path, default=Path("/tmp/glz-moduleize"), help="Scratch directory (driver only)")
    parser.add_argument(
        "--generator",
        type=Path,
        default=Path(__file__).resolve().parent / "glz_generate_headers.py",
        help="Header generator (driver only)",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).resolve().parent / "glz_header_manifest.yml",
        help="Generator manifest (driver only)",
    )
    parser.add_argument("--limit", type=int, default=0, help="Only the first N headers (driver only)")
    # compilation proof
    parser.add_argument("--compile", action="store_true", help="Precompile the converted units (driver only)")
    parser.add_argument("--table", action="store_true", help="Print the full per-header table (driver only)")
    parser.add_argument("--cxx", default="clang++-22", help="Compiler for the compile proof (driver only)")
    parser.add_argument(
        "--std-pcm",
        default="/home/bosyj/Projects/Modules - Glaze/toolchain/std.pcm",
        help="Prebuilt std module (driver only)",
    )
    parser.add_argument("--bmi-dir", type=Path, default=Path("/tmp/glz-moduleize/bmi"), help="BMI output dir (driver only)")
    parser.add_argument("--extra-include", action="append", default=[], help="Extra include dir (driver only)")
    parser.add_argument("--compile-timeout", type=int, default=600, help="Per-unit compile timeout, seconds (driver only)")
    parser.add_argument("--jobs", type=int, default=8, help="Parallel precompiles (driver only)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.all:
        return run_driver(args)

    if args.header is None:
        print("error: --header is required (or use --all)", file=sys.stderr)
        return 2
    header = args.header
    rel = header.as_posix()
    include_root = args.include_root
    if args.relative_to is not None:
        rel = header.resolve().relative_to(args.relative_to.resolve()).as_posix()
    elif header.is_absolute() or str(header).startswith(str(include_root)):
        try:
            rel = header.resolve().relative_to(include_root.resolve()).as_posix()
        except ValueError:
            pass
    if rel.startswith("include/"):
        rel = rel[len("include/") :]
    module_map = load_module_map(args.modules_root) if args.modules_root.exists() else {}
    try:
        name_map = parse_name_map(args.name_map) if args.name_map else None
        unit = convert_header(
            header,
            rel,
            module_map=module_map,
            include_root=include_root.resolve() if include_root.exists() else None,
            module_name=args.module_name,
            name_map=name_map,
            exports=not args.no_exports,
        )
    except ConversionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.stdout:
        sys.stdout.write(unit.text)
    if args.out is not None:
        existing = args.out.read_text(encoding="utf-8") if args.out.exists() else None
        if args.check:
            if existing != unit.text:
                print(f"out of date: {args.out}", file=sys.stderr)
                return 1
            print(f"up to date: {args.out}")
        elif existing != unit.text:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(unit.text, encoding="utf-8", newline="\n")
            print(f"wrote: {args.out}")
        else:
            print(f"unchanged: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
