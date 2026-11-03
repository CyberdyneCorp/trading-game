#!/usr/bin/env python3
"""Fail if engine sources use APIs that break determinism or purity (design D1-D4).

Usage: check_engine_bans.py <dir> [<dir> ...]
"""
import pathlib
import re
import sys

BANNED = [
    (re.compile(r"\bstd::(exp|exp2|expm1|log|log1p|log2|log10|pow|sin|cos|tan|asin|acos|atan2?|sinh|cosh|tanh"
                r"|erfc?|tgamma|lgamma|cbrt|hypot)\b"),
     "platform libm is not bit-identical across targets; use tradelab::det kernels"),
    (re.compile(r"_distribution\b"), "std distributions are implementation-defined; use tradelab PRNG helpers"),
    (re.compile(r"#\s*include\s*<random>"), "use the engine PRNG, not <random>"),
    (re.compile(r"\blong\s+double\b"), "long double width differs per target"),
    (re.compile(r"\bstd::(chrono|thread|random_device|getenv|locale|time)\b"), "engine must not read ambient state"),
    (re.compile(r"\b(getenv|rand|srand)\s*\("), "engine must not read ambient state"),
    (re.compile(r"#\s*include\s*<(fstream|iostream|thread|chrono|ctime|clocale|locale)>"),
     "engine must stay pure: no I/O, threads, clocks or locale"),
]
EXTENSIONS = {".cpp", ".hpp", ".h", ".cc"}


def strip_comments(text: str) -> str:
    # Keep newlines from block comments so reported line numbers stay correct.
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def check_file(path: pathlib.Path) -> list[str]:
    errors = []
    lines = strip_comments(path.read_text(encoding="utf-8")).splitlines()
    for lineno, line in enumerate(lines, start=1):
        for pattern, reason in BANNED:
            if pattern.search(line):
                errors.append(f"{path}:{lineno}: {line.strip()}  -- {reason}")
    return errors


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    errors = []
    for root in argv[1:]:
        for path in sorted(pathlib.Path(root).rglob("*")):
            if path.suffix in EXTENSIONS:
                errors.extend(check_file(path))
    for e in errors:
        print(e)
    print(f"check_engine_bans: {len(errors)} violation(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
