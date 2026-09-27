#!/usr/bin/env python3
"""Generate checked-in Glaze headers from native module interface files.

Builtin-type aliases
--------------------
Glaze module code must not spell the C integer types as bare ``size_t`` /
``uint32_t`` / ... because those names are not guaranteed to be reachable in a
module without pulling in an extra header, and the maintainer explicitly asked
for ``glz::size_t`` / ``glz::uint64_t`` aliases so module code never needs the
``std::`` prefix (AGENTS.md C3).  The public headers, however, use the bare
spelling almost everywhere (at reference commit 4ec2da3d: 2648 bare ``size_t``
vs 114 ``std::size_t``).

To keep the generated headers byte-identical, this generator rewrites the
closed alias set below back to the bare spelling while rendering a module:

    glz::size_t    -> size_t        glz::ptrdiff_t -> ptrdiff_t
    glz::int8_t    -> int8_t        glz::uint8_t   -> uint8_t
    glz::int16_t   -> int16_t       glz::uint16_t  -> uint16_t
    glz::int32_t   -> int32_t       glz::uint32_t  -> uint32_t
    glz::int64_t   -> int64_t       glz::uint64_t  -> uint64_t

The rewrite is intentionally narrow and is NOT a general "dequalify":

  * only these ten aliases are recognised; ``glz::size_type``,
    ``glz::size_t_thing`` and any other token are left untouched;
  * only the exact ``glz::<alias>`` token is matched, with identifier
    boundaries on both sides;
  * ``std::size_t`` and any other ``std::`` spelling is never touched -- where
    the reference header says ``std::size_t`` the module keeps ``std::size_t``;
  * comments and string/char literals are skipped, so documentation that
    mentions ``glz::size_t`` survives as written.

A future reader should not mistake this for magic: the module spelling and the
header spelling differ on purpose, and this table is the single place the
translation is defined.
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
import sys
from dataclasses import dataclass, field
from pathlib import Path


# Closed set of glz:: builtin-type aliases the module side uses; rendered back
# to the bare header spelling.  See the module docstring above.
BUILTIN_TYPE_ALIASES = frozenset(
    {
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
    }
)

HEADER_META_RE = re.compile(r"^\s*//\s*glz:header(?:\s+(?P<body>.*))?$")
MODULE_RE = re.compile(r"^\s*(?:export\s+)?module(?:\s+[A-Za-z_][\w.:]*)?\s*;\s*(?://.*)?$")
IMPORT_RE = re.compile(r"^\s*(?:export\s+)?import\s+(?P<target>[^;]+?)\s*;\s*(?://.*)?$")
# A global-scope `using std::...;` (module-local convenience, never in a header).
GLOBAL_STD_USING_RE = re.compile(r"^using\s+std::[A-Za-z_][\w:]*\s*;\s*(?://.*)?$")
PREPROCESSOR_OPEN_RE = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
PREPROCESSOR_CLOSE_RE = re.compile(r"^\s*#\s*endif\b")
RAW_INCLUDE_RE = re.compile(r"^\s*#\s*include\s+(?P<target>[<\"])(?P<inner>[^>\"]*)[>\"]\s*(?://.*)?$")
IDENT_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


class HeaderGenerationError(RuntimeError):
    pass


@dataclass
class HeaderMetadata:
    path: str | None = None
    std: list[str] = field(default_factory=list)
    includes: list[str] = field(default_factory=list)
    project_imports: str = "include"
    skip: str | None = None


@dataclass
class Manifest:
    import_overrides: dict[str, str] = field(default_factory=dict)


@dataclass
class GeneratedHeader:
    module_name: str | None
    source_path: Path
    header_path: Path
    content: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-root", type=Path, default=Path("modules"), help="Root directory containing .ixx files")
    parser.add_argument("--include-root", type=Path, default=Path("include"), help="Root directory for generated headers")
    parser.add_argument("--manifest", type=Path, help="Optional manifest with import_overrides")
    parser.add_argument(
        "--module",
        action="append",
        default=[],
        help="Generate only a module name or source path. Can be passed more than once.",
    )
    parser.add_argument("--check", action="store_true", help="Fail if generated headers differ from disk")
    parser.add_argument("--dry-run", action="store_true", help="Print generated header paths without writing")
    parser.add_argument(
        "--allow-missing-metadata",
        action="store_true",
        help="Accepted for backwards compatibility; missing metadata is now reported and skipped by default",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail immediately on the first module with missing or incomplete glz:header metadata",
    )
    parser.add_argument(
        "--no-copy-headers",
        action="store_true",
        help="Do not copy existing .hpp/.h support headers from module-root to include-root",
    )
    return parser.parse_args()


def parse_manifest(path: Path | None) -> Manifest:
    if path is None:
        return Manifest()
    if not path.exists():
        raise HeaderGenerationError(f"manifest does not exist: {path}")

    text = path.read_text(encoding="utf-8")
    if path.suffix.lower() == ".json":
        data = json.loads(text)
        return Manifest(import_overrides=dict(data.get("import_overrides", {})))

    import_overrides: dict[str, str] = {}
    section: str | None = None
    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not line.startswith((" ", "\t")):
            if stripped == "import_overrides:":
                section = "import_overrides"
                continue
            section = None
            continue
        if section != "import_overrides":
            continue
        match = re.match(r"\s*([A-Za-z_][\w.:]*)\s*:\s*(.+?)\s*$", line)
        if not match:
            raise HeaderGenerationError(f"{path}:{line_number}: invalid import_overrides entry")
        import_overrides[match.group(1)] = strip_yaml_scalar(match.group(2))

    return Manifest(import_overrides=import_overrides)


def strip_yaml_scalar(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
        return value[1:-1]
    return value


def parse_metadata(source_path: Path, lines: list[str]) -> HeaderMetadata | None:
    metadata = HeaderMetadata()
    found = False

    for line_number, line in enumerate(lines, start=1):
        match = HEADER_META_RE.match(line)
        if not match:
            continue
        found = True
        body = match.group("body") or ""
        if not body.strip():
            continue
        try:
            tokens = shlex.split(body, posix=True)
        except ValueError as exc:
            raise HeaderGenerationError(f"{source_path}:{line_number}: invalid glz:header metadata: {exc}") from exc
        for token in tokens:
            # `skip` may be written bare or with a value; either way the module
            # intentionally has no public header (internal unit).
            if token == "skip":
                metadata.skip = "true"
                continue
            if "=" not in token:
                raise HeaderGenerationError(f"{source_path}:{line_number}: expected key=value metadata, got {token!r}")
            key, value = token.split("=", 1)
            if key == "path":
                metadata.path = value
            elif key == "std":
                metadata.std.append(normalize_std_include(value))
            elif key in {"include", "extra_include", "extra_includes"}:
                metadata.includes.append(normalize_project_include(value))
            elif key == "project_imports":
               if value not in {"include", "ignore"}:
                  raise HeaderGenerationError(
                     f"{source_path}:{line_number}: project_imports must be 'include' or 'ignore'"
                  )
               metadata.project_imports = value
            elif key == "skip":
               metadata.skip = value or "true"
            else:
               raise HeaderGenerationError(f"{source_path}:{line_number}: unknown glz:header key {key!r}")

    if not found:
        return None
    if metadata.skip is not None:
        return metadata
    if metadata.path is None:
        raise HeaderGenerationError(f"{source_path}: glz:header metadata must include path=...")
    return metadata


def normalize_std_include(value: str) -> str:
    value = value.strip()
    if value.startswith("<") and value.endswith(">"):
        return value
    return f"<{value}>"


def normalize_project_include(value: str) -> str:
    value = value.strip()
    if (value.startswith('"') and value.endswith('"')) or (value.startswith("<") and value.endswith(">")):
        return value
    return f'"{value}"'


def discover_module_name(lines: list[str]) -> str | None:
    for line in lines:
        match = re.match(r"^\s*(?:export\s+)?module\s+([A-Za-z_][\w.:]*)\s*;", line)
        if match:
            return match.group(1)
    return None


def module_import_to_include(
    imported_name: str, current_module_name: str | None, manifest: Manifest, source_path: Path
) -> str:
    resolved_name = imported_name
    if imported_name.startswith(":"):
        if current_module_name is None:
            raise HeaderGenerationError(
                f"{source_path}: cannot resolve local partition import {imported_name!r} without a module declaration"
            )
        partition_name = imported_name[1:]
        if not re.fullmatch(r"[A-Za-z_][\w.]*", partition_name):
            raise HeaderGenerationError(f"{source_path}: invalid local partition import {imported_name!r}")
        owning_module = current_module_name.partition(":")[0]
        resolved_name = f"{owning_module}:{partition_name}"

    if resolved_name in manifest.import_overrides:
        return normalize_project_include(manifest.import_overrides[resolved_name])
    if resolved_name == "glaze":
        return '"glaze/glaze.hpp"'
    if imported_name.startswith(":") and resolved_name.startswith("glaze."):
        # Keep partition headers beside their owning module header:
        #   import :fwd; in glaze.json.generic -> glaze/json/generic_fwd.hpp
        owning_module, partition_name = resolved_name.split(":", maxsplit=1)
        header_stem = owning_module.replace(".", "/")
        partition_suffix = partition_name.replace(".", "_")
        return f'"{header_stem}_{partition_suffix}.hpp"'
    if resolved_name.startswith("glaze."):
        return f'"{resolved_name.replace(".", "/")}.hpp"'
    raise HeaderGenerationError(f"{source_path}: unsupported import {imported_name!r}; add an import_overrides entry")


def hoist_include(line: str, std_includes: list[str], project_includes: list[str]) -> bool:
    """Move an unconditional `#include` line into the header's include block.

    Returns True when the line was recognised and consumed.
    """
    match = RAW_INCLUDE_RE.match(line)
    if match is None:
        return False
    target = f"{match.group('target')}{match.group('inner')}{'>' if match.group('target') == '<' else '\"'}"
    if match.group("target") == "<":
        std_includes.append(target)
    else:
        project_includes.append(target)
    return True


def dedupe(items: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for item in items:
        if item in seen:
            continue
        seen.add(item)
        result.append(item)
    return result


def transform_source(
    source_path: Path,
    source_text: str,
    metadata: HeaderMetadata,
    manifest: Manifest,
    include_root: Path,
) -> GeneratedHeader:
    lines = source_text.splitlines()
    module_name = discover_module_name(lines)

    # The file header (licence block) is everything before the first module
    # declaration. Comments *after* the module declaration belong to the body
    # and must stay where they are (after the includes).
    first_decl_index = len(lines)
    for index, line in enumerate(lines):
        if re.match(r"^\s*(?:export\s+)?module(?:\s+[A-Za-z_][\w.:]*)?\s*;", line):
            first_decl_index = index
            break
    prefix = [
        line.replace("glaze.ixx", "glaze.hpp")
        for line in trim_blank_edges(lines[:first_decl_index])
        if not HEADER_META_RE.match(line)
    ]

    std_includes = list(metadata.std)
    project_includes: list[str] = list(metadata.includes)
    import_std_seen = False
    in_global_fragment = False
    conditional_depth = 0
    prelude_lines: list[str] = []
    transformed_lines: list[str] = []

    for index, line in enumerate(lines):
        if index < first_decl_index:
            continue
        if HEADER_META_RE.match(line):
            continue
        if re.match(r"^\s*module\s*;\s*(?://.*)?$", line):
            in_global_fragment = True
            continue
        if in_global_fragment and re.match(r"^\s*export\s+module\s+[A-Za-z_][\w.:]*\s*;\s*(?://.*)?$", line):
            in_global_fragment = False
            continue
        if MODULE_RE.match(line):
            continue

        # Track preprocessor nesting so that conditional includes stay in place
        # (they are part of the surrounding #if block, not the include block).
        if PREPROCESSOR_OPEN_RE.match(line):
            conditional_depth += 1
        elif PREPROCESSOR_CLOSE_RE.match(line):
            conditional_depth = max(0, conditional_depth - 1)

        if in_global_fragment:
            if conditional_depth == 0:
                hoisted = hoist_include(line, std_includes, project_includes)
                if hoisted:
                    continue
            prelude_lines.append(line)
            continue

        import_match = IMPORT_RE.match(line)
        if import_match:
            target = import_match.group("target").strip()
            if target == "std":
                import_std_seen = True
            elif metadata.project_imports == "include":
                include = module_import_to_include(target, module_name, manifest, source_path)
                if conditional_depth == 0:
                    project_includes.append(include)
                else:
                    # The import sits inside a preprocessor guard; keep it there
                    # so the generated header preserves the guard (e.g. the
                    # `#if __cpp_exceptions` block in glaze_exceptions.hpp).
                    transformed_lines.append(f"#include {include}")
            continue
        # Module-local `using std::...;` at global scope exists only so the module
        # body can name std types without the `std::` prefix. Emitting it into a
        # header would pollute every consumer's global namespace (AGENTS.md C3),
        # and the reference headers contain no such declaration.
        if GLOBAL_STD_USING_RE.match(line):
            continue
        if conditional_depth == 0 and hoist_include(line, std_includes, project_includes):
            continue
        transformed_lines.append(line)

    if import_std_seen and not metadata.std:
        # Advisory only.  With the explicit include=/std= contract an empty std
        # list is a legitimate declaration that the public header includes no
        # standard headers (the reference headers very often rely on transitive
        # std includes), so this must not fail --check.  A genuinely missing
        # std= entry shows up as a substantive oracle diff instead.
        print(
            f"note: {source_path}: import std; with no glz:header std=... entry "
            "(no standard includes emitted)",
            file=sys.stderr,
        )

    body = dequalify_builtin_type_aliases(remove_export_tokens("\n".join(transformed_lines)))
    body_lines = collapse_blank_runs(trim_blank_edges(body.splitlines()))
    prelude_lines = dequalify_builtin_type_aliases("\n".join(prelude_lines)).splitlines()
    prelude_lines = collapse_blank_runs(trim_blank_edges(prelude_lines))

    std_include_lines = [f"#include {include}" for include in sorted(dedupe(std_includes))]
    project_include_lines = [f"#include {include}" for include in sorted(dedupe(project_includes))]
    output_lines: list[str] = []
    output_lines.extend(prefix)
    if output_lines and output_lines[-1] != "":
        output_lines.append("")
    output_lines.append("#pragma once")
    if std_include_lines:
        output_lines.append("")
        output_lines.extend(std_include_lines)
    if project_include_lines:
        output_lines.append("")
        output_lines.extend(project_include_lines)
    if prelude_lines:
        output_lines.append("")
        output_lines.extend(prelude_lines)
    if body_lines:
        output_lines.append("")
        output_lines.extend(body_lines)

    header_path = resolve_header_path(include_root, metadata.path)
    return GeneratedHeader(module_name=module_name, source_path=source_path, header_path=header_path, content="\n".join(output_lines) + "\n")


def trim_blank_edges(lines: list[str]) -> list[str]:
    start = 0
    end = len(lines)
    while start < end and lines[start].strip() == "":
        start += 1
    while end > start and lines[end - 1].strip() == "":
        end -= 1
    return lines[start:end]


def collapse_blank_runs(lines: list[str], max_run: int = 1) -> list[str]:
    result: list[str] = []
    blank_run = 0
    for line in lines:
        if line.strip() == "":
            blank_run += 1
            if blank_run <= max_run:
                result.append(line)
            continue
        blank_run = 0
        result.append(line)
    return result


def resolve_header_path(include_root: Path, metadata_path: str) -> Path:
    path = Path(metadata_path)
    if path.is_absolute():
        return path
    return include_root / path


def remove_export_tokens(text: str) -> str:
    result: list[str] = []
    index = 0
    length = len(text)
    line_start = True
    preprocessor_line = False

    while index < length:
        ch = text[index]

        if line_start:
            lookahead = index
            while lookahead < length and text[lookahead] in " \t":
                lookahead += 1
            preprocessor_line = lookahead < length and text[lookahead] == "#"
            line_start = False

        if ch == "\n":
            result.append(ch)
            index += 1
            line_start = True
            preprocessor_line = False
            continue

        if ch == "/" and index + 1 < length and text[index + 1] == "/":
            end = text.find("\n", index)
            if end == -1:
                result.append(text[index:])
                break
            result.append(text[index:end])
            index = end
            continue

        if ch == "/" and index + 1 < length and text[index + 1] == "*":
            end = text.find("*/", index + 2)
            if end == -1:
                result.append(text[index:])
                break
            result.append(text[index : end + 2])
            index = end + 2
            continue

        if ch == "R" and index + 1 < length and text[index + 1] == '"':
            raw_end = find_raw_string_end(text, index)
            if raw_end is not None:
                result.append(text[index:raw_end])
                index = raw_end
                continue

        if ch == "'" and is_digit_separator(text, index):
            result.append(ch)
            index += 1
            continue

        if ch in {'"', "'"}:
            literal_end = find_quoted_literal_end(text, index, ch)
            result.append(text[index:literal_end])
            index = literal_end
            continue

        if not preprocessor_line and (ch.isalpha() or ch == "_"):
            end = index + 1
            while end < length and text[end] in IDENT_CHARS:
                end += 1
            token = text[index:end]
            if token == "export":
                index = end + 1 if end < length and text[end] in " \t" else end
                continue
            result.append(token)
            index = end
            continue

        result.append(ch)
        index += 1

    return "".join(result)


def dequalify_builtin_type_aliases(text: str) -> str:
    """Render ``glz::<builtin-alias>`` back to the bare header spelling.

    Only the ten aliases in ``BUILTIN_TYPE_ALIASES`` are translated, only when
    written as the exact token ``glz::alias`` (identifier boundaries on both
    sides), and never inside comments or string/char literals.  ``std::``
    spellings are left alone.  See the module docstring for why this exists.
    """
    result: list[str] = []
    index = 0
    length = len(text)

    while index < length:
        ch = text[index]

        if ch == "/" and index + 1 < length and text[index + 1] == "/":
            end = text.find("\n", index)
            if end == -1:
                result.append(text[index:])
                break
            result.append(text[index:end])
            index = end
            continue

        if ch == "/" and index + 1 < length and text[index + 1] == "*":
            end = text.find("*/", index + 2)
            if end == -1:
                result.append(text[index:])
                break
            result.append(text[index : end + 2])
            index = end + 2
            continue

        if ch == "R" and index + 1 < length and text[index + 1] == '"':
            raw_end = find_raw_string_end(text, index)
            if raw_end is not None:
                result.append(text[index:raw_end])
                index = raw_end
                continue

        if ch == "'" and is_digit_separator(text, index):
            result.append(ch)
            index += 1
            continue

        if ch in {'"', "'"}:
            literal_end = find_quoted_literal_end(text, index, ch)
            result.append(text[index:literal_end])
            index = literal_end
            continue

        if ch.isalpha() or ch == "_":
            end = index + 1
            while end < length and text[end] in IDENT_CHARS:
                end += 1
            token = text[index:end]
            # Match only the exact `glz::<alias>` sequence.  `glz` must be
            # immediately followed by `::` and the alias must be a complete
            # identifier, so `glz::size_type`/`glz::size_t_thing` do not match.
            if token == "glz" and text[end : end + 2] == "::":
                alias_end = end + 2
                while alias_end < length and text[alias_end] in IDENT_CHARS:
                    alias_end += 1
                alias = text[end + 2 : alias_end]
                if alias in BUILTIN_TYPE_ALIASES:
                    result.append(alias)
                    index = alias_end
                    continue
            result.append(token)
            index = end
            continue

        result.append(ch)
        index += 1

    return "".join(result)


def is_digit_separator(text: str, index: int) -> bool:
    return (
        index > 0
        and index + 1 < len(text)
        and (text[index - 1].isalnum() or text[index - 1] == "_")
        and (text[index + 1].isalnum() or text[index + 1] == "_")
    )


def find_quoted_literal_end(text: str, start: int, quote: str) -> int:
    index = start + 1
    while index < len(text):
        if text[index] == "\\":
            index += 2
            continue
        if text[index] == quote:
            return index + 1
        index += 1
    return len(text)


def find_raw_string_end(text: str, start: int) -> int | None:
    delimiter_end = text.find("(", start + 2)
    if delimiter_end == -1:
        return None
    delimiter = text[start + 2 : delimiter_end]
    if len(delimiter) > 16 or any(ch.isspace() or ch in "\\()" for ch in delimiter):
        return None
    terminator = ")" + delimiter + '"'
    end = text.find(terminator, delimiter_end + 1)
    if end == -1:
        return len(text)
    return end + len(terminator)


def generate_headers(args: argparse.Namespace) -> tuple[list[GeneratedHeader], list[str]]:
    manifest = parse_manifest(args.manifest)
    source_root = args.module_root
    include_root = args.include_root
    selected = set(args.module)
    generated: list[GeneratedHeader] = []
    generated_paths: dict[Path, Path] = {}
    problems: list[str] = []
    skipped = 0

    for source_path in sorted(source_root.rglob("*.ixx")):
        source_text = source_path.read_text(encoding="utf-8")
        lines = source_text.splitlines()
        metadata = parse_metadata(source_path, lines)
        if metadata is None:
            message = f"{source_path}: missing glz:header metadata"
            if args.strict:
                raise HeaderGenerationError(message)
            problems.append(message)
            continue
        if metadata.skip is not None:
            skipped += 1
            continue
        header = transform_source(source_path, source_text, metadata, manifest, include_root)
        if selected and not matches_selection(header, selected):
            continue
        previous = generated_paths.get(header.header_path)
        if previous is not None:
            raise HeaderGenerationError(
                f"{source_path}: duplicate generated header path {header.header_path} also used by {previous}"
            )
        generated_paths[header.header_path] = source_path
        generated.append(header)

    if not args.no_copy_headers:
        generated.extend(copy_support_headers(source_root, include_root, generated_paths, selected))
    if skipped:
        print(f"skipped {skipped} module file(s) with glz:header skip metadata")
    for problem in problems:
        print(f"warning: {problem}", file=sys.stderr)
    return generated, problems


def sanitize_copied_header(text: str) -> str:
    """Strip module-conversion artefacts from a copied support header.

    Support headers were carried through the module conversion too, so they can
    contain the same global-scope `using std::...;` pollution and the module
    provenance line ("refer to glaze.ixx") as the generated headers. The
    reference tree contains neither.
    """
    lines = text.splitlines()
    result_lines: list[str] = []
    drop_following_blank = False
    for line in lines:
        if GLOBAL_STD_USING_RE.match(line):
            # The declaration was inserted before a blank separator; drop the
            # now-redundant blank so single-blank spacing is preserved.
            drop_following_blank = True
            continue
        if drop_following_blank and line.strip() == "":
            drop_following_blank = False
            continue
        drop_following_blank = False
        result_lines.append(line.replace("glaze.ixx", "glaze.hpp"))
    while result_lines and result_lines[-1].strip() == "":
        result_lines.pop()
    result = "\n".join(result_lines)
    if result and text.endswith("\n"):
        result += "\n"
    return result


def copy_support_headers(
    source_root: Path, include_root: Path, generated_paths: dict[Path, Path], selected: set[str]
) -> list[GeneratedHeader]:
    copied: list[GeneratedHeader] = []
    for source_path in sorted(list(source_root.rglob("*.hpp")) + list(source_root.rglob("*.h"))):
        header_path = include_root / source_path.relative_to(source_root)
        if header_path in generated_paths:
            continue
        header = GeneratedHeader(
            module_name=None,
            source_path=source_path,
            header_path=header_path,
            content=sanitize_copied_header(source_path.read_text(encoding="utf-8")),
        )
        if selected and not matches_selection(header, selected):
            continue
        copied.append(header)
    return copied


def matches_selection(header: GeneratedHeader, selected: set[str]) -> bool:
    source = header.source_path.as_posix()
    destination = header.header_path.as_posix()
    owning_module = header.module_name.partition(":")[0] if header.module_name else None
    return any(
        item == header.module_name or item == owning_module or item in source or item in destination for item in selected
    )


def write_or_check(headers: list[GeneratedHeader], check: bool, dry_run: bool, problems: list[str]) -> int:
    changed: list[GeneratedHeader] = []
    for header in headers:
        existing = header.header_path.read_text(encoding="utf-8") if header.header_path.exists() else None
        if existing != header.content:
            changed.append(header)
        if dry_run:
            status = "would update" if existing != header.content else "up to date"
            print(f"{status}: {header.header_path}")
        elif not check and existing != header.content:
            header.header_path.parent.mkdir(parents=True, exist_ok=True)
            header.header_path.write_text(header.content, encoding="utf-8", newline="\n")
            print(f"generated: {header.header_path}")

    failed = False
    if check and problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        failed = True
    if check and changed:
        for header in changed:
            print(f"out of date: {header.header_path}", file=sys.stderr)
        failed = True
    if failed:
        return 1
    if check:
        print(f"checked {len(headers)} header(s)")
    elif not dry_run:
        print(f"generated {len(headers)} header(s)")
    return 0


def main() -> int:
    args = parse_args()
    try:
        headers, problems = generate_headers(args)
        if not headers:
            print("no module files with glz:header metadata found", file=sys.stderr)
            return 1
        return write_or_check(headers, check=args.check, dry_run=args.dry_run, problems=problems)
    except HeaderGenerationError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
