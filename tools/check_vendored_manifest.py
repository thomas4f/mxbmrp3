#!/usr/bin/env python3
"""Vendored-manifest drift check: vendored.json vs the files actually vendored.

The weekly vendored-deps workflow compares the manifest against UPSTREAM
latest, but nothing stopped the other drift: re-vendoring a library and
forgetting the manifest bump, after which the release SBOM (tools/gen_sbom.py)
misreports what ships and the freshness check compares the wrong baseline —
exactly the gap mxbmrp3/vendor/vendored.json exists to close.

This script closes it mechanically: each manifest entry's `version` is compared
against the version the vendored source SELF-declares (its version macro, or
the versioned banner comment for libs without one). Runs in the unit-tests CI
job; pure stdlib, no network.

A manifest entry with no extractor registered below is reported loudly (and
fails the check) rather than skipped, so a newly vendored library can't create
a silent blind spot — add an extractor when you add the dependency.

Usage: python3 tools/check_vendored_manifest.py   (from the repo root)
Exit codes: 0 = manifest matches sources, 1 = drift (or uncheckable entry).
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "mxbmrp3" / "vendor" / "vendored.json"


def _grep(path: Path, pattern: str) -> str:
    """First capture group of `pattern` in `path`, or '' if absent."""
    m = re.search(pattern, path.read_text(encoding="utf-8", errors="replace"))
    return m.group(1) if m else ""


def _defines(path: Path, prefix: str) -> str:
    """Join MAJOR/MINOR/PATCH #defines with `prefix` into 'x.y.z'."""
    text = path.read_text(encoding="utf-8", errors="replace")
    parts = []
    for field in ("MAJOR", "MINOR", "PATCH"):
        m = re.search(rf"#define\s+{prefix}_{field}\s+(\d+)", text)
        if not m:
            return ""
        parts.append(m.group(1))
    return ".".join(parts)


# Manifest `name` -> callable(dep) returning the self-declared version string.
# `path` in the manifest may be a file or a directory (miniz); each extractor
# knows which file inside carries the version.
EXTRACTORS = {
    "nlohmann/json": lambda p: _defines(p, "NLOHMANN_JSON_VERSION"),
    "cpp-httplib": lambda p: _grep(p, r'#define\s+CPPHTTPLIB_VERSION\s+"([\d.]+)"'),
    "doctest": lambda p: _defines(p, "DOCTEST_VERSION"),
    # stb has no version macro; the header's own banner comment is the source
    # of truth (matches the manifest's repoHasReleases note).
    "stb_truetype": lambda p: _grep(p, r"stb_truetype\.h - v([\d.]+)"),
    # miniz's MZ_VERSION macro is the internal ABI version (11.x — see the
    # manifest note); the RELEASE version is the banner on miniz.h line 1.
    "miniz": lambda p: _grep(p / "miniz.h", r"miniz\.c\s+([\d.]+)\s"),
}


def main() -> int:
    deps = json.loads(MANIFEST.read_text(encoding="utf-8"))["deps"]
    failed = False
    for dep in deps:
        name, pinned = dep["name"], dep["version"]
        path = ROOT / dep["path"]
        extractor = EXTRACTORS.get(name)
        if extractor is None:
            print(f"FAIL  {name}: no version extractor registered in "
                  f"tools/check_vendored_manifest.py — add one for the new dependency")
            failed = True
            continue
        if not path.exists():
            print(f"FAIL  {name}: manifest path {dep['path']} does not exist")
            failed = True
            continue
        actual = extractor(path)
        if not actual:
            print(f"FAIL  {name}: could not extract a version from {dep['path']} "
                  f"(extractor pattern no longer matches?)")
            failed = True
        elif actual != pinned:
            print(f"FAIL  {name}: manifest pins {pinned} but {dep['path']} "
                  f"self-declares {actual} — bump `version` in vendored.json")
            failed = True
        else:
            print(f"ok    {name}: {pinned}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
