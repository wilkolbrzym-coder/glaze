# glz_moduleize — header → C++20 module converter

Author: agent/moduleizer. Commits `31122382b` … `88c3bd06e` (only `tools/glz_moduleize.py`).

## 1. Interface

```
python3 tools/glz_moduleize.py --header include/glaze/a/b.hpp --out modules/glaze/a/b.ixx
python3 tools/glz_moduleize.py --header H --stdout
python3 tools/glz_moduleize.py --check --header H --out M
# driver (whole tree, round-trip + optional compile proof):
python3 tools/glz_moduleize.py --all --include-root REF --modules-root modules \
        --work DIR [--compile --bmi-dir DIR2 --jobs N --table]
```

`--all` converts every reference header that has a module unit, writes the units to
`DIR/gen_modules`, runs `tools/glz_generate_headers.py` over them, and diffs each
regenerated header against the input; with `--compile` it also precompiles every unit
(clang++-22, libc++-22, prebuilt `std.pcm`) in dependency order and prints per-unit
round-trip / compiles / differs-from-hand columns plus a TSV at `DIR/report.tsv`.

## 2. Rules the converter derives from the input (no per-file table)

* **Module name.** Mechanically `glaze/a/b.hpp` → `glaze.a.b` (AGENTS.md C5). Eight
  existing units are *not* path-derivable (`glaze/glaze.hpp`→`glaze`,
  `glaze/glaze_exceptions.hpp`→`glaze.exceptions`, `glaze/tuplet/tuple.hpp`→`glaze.tuplet`,
  `glaze/json/generic_fwd.hpp`→`glaze.json.generic:fwd`, and `{base64,compare,stencil,trace}/X.hpp`
  → `glaze.{base64,compare,stencil,trace}`); for those
  the name is read from the module tree (`glz:header path=` + declaration), i.e. from the
  repository state, not from a literal in the code.
* **Framing.** Licence block, `#pragma once`, `license_gap`, `license=none`,
  `pragma_once=none`, `blank_runs`, `trailing_blanks`, `trailing_newline` are all measured
  from the input.
* **Pre-code region.** Everything before the first non-blank/non-comment/non-preprocessor
  line. If its preprocessor conditionals are balanced, the whole region is lifted into the
  unit's **global module fragment** (rendered in place with a `// glz:emit prelude`
  forcer); if not, a body mode is used.
* **Includes.** Every `#include` (prelude + mid-body, any nesting) is found;
  * in a balanced prelude it stays literal in the fragment (it both renders and compiles);
  * anywhere else — where a textual include would attach foreign declarations to this
    module — it is replaced by a `// glz:emit <group>` marker plus `glz:header` metadata,
    and a resolved copy is compiled inside a `// glz:module-only` fragment block wrapped in
    the include's own preprocessor guards;
  * a bare sibling name (`"types.hpp"`) is resolved against the header's directory.
  Runs are cut into chunks whose spelling is already sorted, because the generator sorts
  each metadata block — this preserves the header's original include order.
* **`export`.** A conservative, scope-aware scan marks namespace-scope declarations that
  start with a declaration keyword and cannot carry internal linkage; namespace
  definitions, `static`, out-of-class member/operator definitions, macro continuations and
  non-declaration statements are skipped.
* **`glz::` aliases** (AGENTS.md C3). Bare `size_t` / `uint64_t` / … in code are spelled
  `glz::…` (the exact inverse of the generator's rewrite), skipping comments, literals and
  preprocessor lines; `import glaze.core.basic_types;` is added when used.

## 3. Evidence

All commands run from `/home/bosyj/Projects/Modules - Glaze/work/moduleizer`.
Reference tree: `4ec2da3d` (the tree the oracle and the checked-in modules reproduce),
extracted to `scratch/moduleizer/ref/include`. The worktree `include/` is a *restored main*
tree (commit `541a52ea0`) that differs from `4ec2da3d` in 135 files, so the converter is
fed the reference tree, not the worktree headers.

Round-trip (regenerates every header from the converted units and diffs):

```
$ python3 tools/glz_moduleize.py --all --include-root .../ref/include --modules-root modules --work /tmp/glz-mod9
== glz_moduleize driver ==
  headers converted      : 249
  round-trip byte-exact  : 249/249
```

Determinism:

```
$ diff -rq /tmp/det1/gen_modules /tmp/det2/gen_modules
(no output)  ->  all 249 units byte-identical across runs
```

Oracle (modules/ untouched by this work):

```
$ /home/bosyj/Projects/Modules - Glaze/oracle/check_fidelity.sh "$(pwd)"
  generator ok (250 headers produced)
  byte-identical        : 251
  extra (not in ref)    : 0
RESULT: PASS - include/ is exactly reproducible from modules/
```

Compile proof (clang++-22, `--compile`, fixpoint BMI build):

```
== glz_moduleize driver ==
  headers converted      : 249
  round-trip byte-exact  : 249/249
  precompile OK          : 216/249
  identical to hand unit : 0/249
```

`identical to hand unit = 0` is expected: the converter emits one uniform shape, the
checked-in units were each curated by hand. Round-trip and compilation are what hold, not
textual identity.

## 4. Per-unit outcome

Full table: `scratch/moduleizer/final/work/report.tsv` (columns
`header / roundtrip / compiles / hand / detail`), produced by
`--all --compile --table`. Summary: **249/249 round-trip, 216/249 precompile.**

### 4a. Environment-limited (15) — third-party headers absent, or API removed on main

`glaze/eetf/{cmp,defs,eetf_to_json,ei,read,types,wrappers,write}.hpp` (needs `ei.h`),
`glaze/ext/eigen.hpp` (Eigen), `glaze/ext/glaze_asio.hpp`,
`glaze/net/{http_client,http_server,websocket_client,websocket_connection}.hpp` (Asio),
`glaze/thread/async_vector.hpp` (`value_proxy.hpp`, removed on main). These fail on their
missing include before any module concern; the hand units fail the same way in the existing
harness. Not converter defects.

### 4b. Pre-existing header self-containment defects (13) — the header itself does not compile

These fail when compiled **header-only** with only their own includes on the search path,
so a module unit that includes exactly what the header declares cannot compile either.
Verified with `clang++-22 -fsyntax-only` on `#include "glaze/X.hpp"`:

| header | header-only error |
|---|---|
| `glaze/util/dump.hpp` | `unknown type name 'string_literal'` (uses it without including `util/string_literal.hpp`) |
| `glaze/core/write_chars.hpp`, `glaze/util/validate.hpp` | same, via `dump.hpp` |
| `glaze/util/variant.hpp` | `unknown type name 'GLZ_ALWAYS_INLINE'` |
| `glaze/beve/wrappers.hpp`, `glaze/file/raw_or_file.hpp` | `no template named 'op' in glz::parse` |
| `glaze/csv/write.hpp` | `no template named 'reflect'` |
| `glaze/stencil/stencil.hpp` | `no member named 'op' in glz::serialize` |
| `glaze/hardware/volatile_array.hpp` | `no template named 'reverse_iterator' in namespace std` |
| `glaze/net/rest_registry_impl.hpp`, `glaze/rpc/jsonrpc_registry_impl.hpp`, `glaze/rpc/repe/repe_registry_impl.hpp` | `explicit specialization of undeclared template` |
| `glaze/rpc/registry.hpp` | via `rest_registry_impl.hpp` |

These headers rely on a transitive include order that only exists when the umbrella
(`glaze/glaze.hpp`) is included first. The checked-in modules compensated with extra manual
imports; a mechanical converter cannot invent them.

### 4c. Converter residual (5)

| header | error | class |
|---|---|---|
| `glaze/api/type_support.hpp` | `expected template` | `export` placed on a declaration whose `requires`-expression contains a `;`, splitting one template declaration into two `export`s |
| `glaze/json/generic.hpp` | `declaration of 'dump' in module … follows declaration in the global module` | `*_fwd.hpp` + impl split: the forward declaration is textually included in the fragment (global module) while the definition is in the module purview |
| `glaze/json/read.hpp` | same, `quoted_t` | same class |
| `glaze/util/fast_float.hpp`, `glaze/util/zmij.hpp` | `unknown type name '__m128i'` | a platform intrinsic is included inside an `#else` branch; the fragment guard copy reproduces only the `#if` condition, so the branch is never compiled |

## 5. Headers that cannot be moduleized mechanically — fundamental reasons

* **`glaze/util/inline.hpp`** — there is no module unit and there cannot be one: the module
  name would be `glaze.util.inline`, but `inline` is a keyword and a module name is a
  dot-separated sequence of *identifiers*. Proof:
  `export module glaze.util.inline;` → `error: expected a module name after 'module'`.
  The tree resolves this by keeping `inline.hpp` as a copied support header (the generator's
  `copy_support_headers` path).
* **The four headers removed on main** (`file/hostname_include.hpp`,
  `thread/shared_async_map.hpp`, `thread/shared_async_vector.hpp`, `thread/value_proxy.hpp`)
  have no unit by design; the oracle skips them.
* **The 13 headers of §4b** cannot be moduleized from the header alone: the header under-
  declares its dependencies, and the module must add imports the header never names. That is
  a semantic decision, not a textual transformation.
* **`glaze/json/generic_fwd.hpp` / `generic.hpp` and `json/read.hpp`** require the
  forward-declaration/definition split to live in modules (partition/import), not in the
  global module fragment — the declaration and the definition must be in the *same* module.
* **`#else`-guarded platform includes** (`fast_float`, `zmij`) need branch-aware guard
  reproduction; reproducing only the `#if` condition compiles the wrong branch.

## 6. Verdict

* **Round-trip is fully mechanical today.** 249/249 reference headers are regenerated
  byte-for-byte from converter output, deterministically, with no per-file table. The `.ixx`
  files can be treated as generated artifacts *for the purpose of reproducing `include/`*.
* **Compilation is not yet 100% mechanical.** 216/249 units precompile; of the 33 failures,
  15 are missing third-party headers / removed API, 13 are pre-existing defects in the
  reference headers themselves (they do not compile header-only either), and 5 are converter
  gaps (two structural: `#else` guards and fwd/impl splits; one `export` edge case; two of the
  five overlap the structural classes). So the whole `.ixx` tree cannot yet be declared
  "generated and correct" end-to-end.
* To close the gap the toolchain needs: (1) branch-aware guard reproduction or importing
  the module for `#else`-guarded deps, (2) module imports (or partitions) for
  forward-declaration headers whose definitions live in another header, (3) a parser-grade
  `export` placer, and (4) a fix to the 13 non-self-contained headers (a *header* bug:
  `include/` does not satisfy C1 for those files today).

## 7. Forbidden paths

Nothing under `include/**`, `tests/**`, `CMakeLists.txt`, `CMakePresets.json`,
`.github/**`, `cmake/**`, `docs/**`, or `tools/glz_generate_headers.py` was changed.
`git status` is clean; every commit on this branch touches only `tools/glz_moduleize.py`.
