#!/usr/bin/env python3
"""Record the public releases' download counts for the usage survey report.

Writes usage_survey/downloads.json: one row per published release of the PUBLIC
repo (thomas4f/mxbmrp3), with its publish date and the downloads of its
installer and zip summed. The SBOM (`*.cdx.json`) is left out - it is a release
artifact, not a way to get the plugin, and counting it would inflate the total
with tooling fetches.

`tools/analytics_report.py` reads the file for the report's Downloads and
Releases figures, and for the release-line table's dates and downloads. Kept as
its own committed file, rather than fetched by the report, so the report stays a
deterministic, offline render of committed inputs (its selftest needs no network).

WHY THIS IS BESPOKE: one paginated GET against the documented releases API; the
`gh` CLI could make the call but is not on the usage-survey runner's path for
anything else, and the summing and the stable output format are this file either
way. GITHUB_TOKEN, when set, only raises the API rate limit - the repo is public.

The output is byte-stable (sorted, fixed key order, trailing newline), so a day
with no new downloads leaves no git diff.
"""

import json
import os
import re
import sys
import urllib.request

REPO = "thomas4f/mxbmrp3"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "usage_survey", "downloads.json")


def counted(asset_name):
    """The installer and the zip are downloads of the plugin; anything else
    attached to a release (the SBOM) is not."""
    return asset_name.endswith(".exe") or asset_name.endswith(".zip")


def version_key(tag):
    return tuple(int(p) for p in re.findall(r"\d+", tag))


def fetch(repo=REPO):
    releases, page = [], 1
    token = os.environ.get("GITHUB_TOKEN")
    while True:
        req = urllib.request.Request(
            "https://api.github.com/repos/{}/releases?per_page=100&page={}".format(repo, page),
            headers={"Accept": "application/vnd.github+json",
                     "User-Agent": "mxbmrp3-usage-survey"})
        if token:
            req.add_header("Authorization", "Bearer " + token)
        with urllib.request.urlopen(req, timeout=60) as resp:
            batch = json.load(resp)
        if not isinstance(batch, list):
            sys.exit("error: unexpected releases API response: {}".format(str(batch)[:200]))
        releases.extend(batch)
        if len(batch) < 100:
            return releases
        page += 1


def summarise(releases):
    rows = [{"tag": r["tag_name"],
             "published": (r.get("published_at") or "")[:10],
             "downloads": sum(a["download_count"] for a in r.get("assets", [])
                              if counted(a["name"]))}
            for r in releases if not r.get("draft")]
    rows.sort(key=lambda x: version_key(x["tag"]), reverse=True)
    return {"repo": REPO, "releases": rows}


def write(data, path=OUT):
    with open(path, "w") as f:
        f.write(json.dumps(data, indent=1) + "\n")


def main():
    data = summarise(fetch())
    if not data["releases"]:
        sys.exit("error: no releases returned for {}".format(REPO))
    write(data)
    print("Wrote {}: {} releases, {:,} downloads".format(
        OUT, len(data["releases"]), sum(x["downloads"] for x in data["releases"])))


if __name__ == "__main__":
    main()
