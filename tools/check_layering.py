#!/usr/bin/env python3
"""Cross-platform source-layering checks for 3D Claw."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class LayerCheck:
    name: str
    pattern: str
    roots: tuple[str, ...]
    glob: str | None = None


CHECKS: tuple[LayerCheck, ...] = (
    LayerCheck(
        name="app must not include CGAL or Easy3D algorithm headers",
        pattern=r'#include\s+[<"](easy3d/algo|easy3d/kdtree|CGAL|algorithms/cgal)',
        roots=("src/app",),
    ),
    LayerCheck(
        name="app CMake target must not expose or link CGAL runners",
        pattern=r"claw3d_cgal_algorithms|src/algorithms/cgal/include",
        roots=("src/app/CMakeLists.txt",),
    ),
    LayerCheck(
        name="app must not include or own concrete runner types",
        pattern=(
            r'#include\s+".*runner\.h"|#include\s+".*_runner\.h"|'
            r'std::shared_ptr<.*Runner>|\b[A-Za-z0-9]+Runner\b'
        ),
        roots=("src/app",),
    ),
    LayerCheck(
        name="public service headers must not expose concrete runners",
        pattern=r"runner\.h|_runner\.h|std::shared_ptr<.*Runner>",
        roots=("src/services",),
        glob="*.h",
    ),
    LayerCheck(
        name="backend layers must not include app or UI headers",
        pattern=r'#include\s+[<"](imgui|GLFW|app/)',
        roots=("src/services", "src/algorithms", "src/io", "src/common"),
    ),
)


def iter_files(repo_root: Path, roots: Iterable[str], glob: str | None) -> Iterable[Path]:
    for root_name in roots:
        root = repo_root / root_name
        if not root.exists():
            continue
        if root.is_file():
            yield root
            continue
        if glob:
            yield from (path for path in root.rglob(glob) if path.is_file())
        else:
            yield from (path for path in root.rglob("*") if path.is_file())


def scan_file(path: Path, pattern: re.Pattern[str], repo_root: Path) -> list[str]:
    hits: list[str] = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        return [f"{path}: error reading file: {exc}"]

    rel_path = path.relative_to(repo_root)
    for line_no, line in enumerate(text.splitlines(), start=1):
        if pattern.search(line):
            hits.append(f"{rel_path}:{line_no}: {line.strip()}")
    return hits


def run_checks(repo_root: Path, verbose: bool) -> int:
    failed = False
    for check in CHECKS:
        print(f"== {check.name}")
        pattern = re.compile(check.pattern)
        hits: list[str] = []
        for path in iter_files(repo_root, check.roots, check.glob):
            hits.extend(scan_file(path, pattern, repo_root))

        if hits:
            failed = True
            print("FAILED: layering violation found.")
            for hit in hits:
                print(hit)
        elif verbose:
            print("OK: no matches.")

    if failed:
        print("Layering check failed.")
        return 1
    print("Layering check passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    return run_checks(args.repo_root.resolve(), args.verbose)


if __name__ == "__main__":
    sys.exit(main())
