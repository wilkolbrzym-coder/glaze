#!/usr/bin/env python3
"""Rewrite behavioural test sources from the module path to the header path.

For every tests/modules/behaviour/*.cpp, each line

    import glaze.a.b;

becomes

    #include "glaze/a/b.hpp"

and the result is written to <outdir>. This does NOT test the modules; it is a
cross-check that the assertion logic in each behavioural test is correct
against the reference header implementation, so that when the module units
compile the tests are known-good. The runner labels these results
HEADER-CROSSCHECK and never mistakes them for module results.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

IMPORT = re.compile(r"^import (glaze\.[A-Za-z_][A-Za-z0-9_.]*);\s*$", re.M)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    produced = []
    for p in sorted(args.src.glob("*.cpp")):
        text = p.read_text()

        def repl(m: re.Match) -> str:
            return '#include "' + m.group(1).replace(".", "/") + '.hpp"'

        new = IMPORT.sub(repl, text)
        if new == text and "import " in text:
            # an import the regex did not understand; refuse rather than emit a broken file
            print(f"header_crosscheck: unrecognised import in {p}", file=sys.stderr)
            return 2
        out = args.out / p.name
        out.write_text(new)
        produced.append(str(out))
    print("\n".join(produced))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
