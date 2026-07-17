#!/usr/bin/env python3
# ============================================================================
# tools/gen_sbom.py
# Emit a CycloneDX 1.6 SBOM for a release, driven off mxbmrp3/vendor/vendored.json
# (the single source of truth for our vendored C/C++ dependencies).
#
# GitHub's dependency graph / Dependabot don't see vendored C++ source, so this
# is how the release documents what third-party code is actually inside the DLL.
# 'shipped' deps (compiled into mxbmrp3.dlo) are scope=required; the rest (e.g.
# doctest, test-only) are scope=optional so tooling isn't conflated with product.
#
# Usage: python tools/gen_sbom.py --version 1.27.0.267 --out dist/mxbmrp3.cdx.json
# ============================================================================
import argparse
import datetime
import json
import os
import sys
import uuid

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO_ROOT, "mxbmrp3", "vendor", "vendored.json")


def component(dep):
    repo = dep.get("repo") or ""
    ver = dep["version"]
    c = {
        "type": "library",
        "name": dep["name"],
        "version": ver,
        "scope": "required" if dep.get("shipped") else "optional",
    }
    if repo:
        # A versioned github purl (pkg:github/owner/repo@X.Y) only resolves when the
        # repo actually tags releases. For repos that don't (repoHasReleases: false,
        # e.g. nothings/stb — its version is the header's own comment), emit the
        # unversioned purl; the component's "version" field still carries it.
        if dep.get("repoHasReleases") is False:
            c["purl"] = "pkg:github/{}".format(repo)
        else:
            c["purl"] = "pkg:github/{}@{}".format(repo, ver)
        c["externalReferences"] = [
            {"type": "vcs", "url": "https://github.com/{}".format(repo)}
        ]
    props = [{"name": "mxbmrp3:vendored-path", "value": dep["path"]}]
    props.append({"name": "mxbmrp3:shipped", "value": "true" if dep.get("shipped") else "false"})
    if dep.get("note"):
        props.append({"name": "mxbmrp3:note", "value": dep["note"]})
    c["properties"] = props
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="mxbmrp3 release version (e.g. 1.27.0.267)")
    ap.add_argument("--out", help="output path (default: stdout)")
    ap.add_argument("--timestamp", help="RFC3339 UTC timestamp (default: now)")
    args = ap.parse_args()

    with open(MANIFEST, encoding="utf-8") as f:
        manifest = json.load(f)

    ts = args.timestamp or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": "urn:uuid:" + str(uuid.uuid4()),
        "version": 1,
        "metadata": {
            "timestamp": ts,
            "tools": {"components": [{"type": "application", "name": "gen_sbom.py", "group": "mxbmrp3"}]},
            "component": {
                "type": "application",
                "name": "mxbmrp3",
                "version": args.version,
                "description": "HUD, immersion, and streaming plugin for MX Bikes, GP Bikes, and Kart Racing Pro.",
                "externalReferences": [
                    {"type": "vcs", "url": "https://github.com/thomas4f/mxbmrp3"}
                ],
            },
        },
        "components": [component(d) for d in manifest.get("deps", [])],
    }

    out = json.dumps(bom, indent=2)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(out + "\n")
        shipped = sum(1 for d in manifest.get("deps", []) if d.get("shipped"))
        print("SBOM written: {} ({} components, {} shipped)".format(
            args.out, len(bom["components"]), shipped))
    else:
        print(out)


if __name__ == "__main__":
    sys.exit(main())
