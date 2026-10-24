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

It also keeps usage_survey/downloads_history.csv, one `date,downloads` row per
UTC day with that day's total, for the report's Downloads charts. GitHub
keeps only a running count per asset, with no history, so this daily sample is
the only record of how the total grew. A day keeps its FIRST sample: the
scheduled run takes it early in the UTC day, and a manual rerun later that day
must not move it (it leaves downloads.json alone too, so the tile and the
release-line table stay on the same sample), or the day before stretches to cover the extra hours and the
day itself shrinks to the rest (Oct 16, rerun at 19:30, read 450 then 111).
Rows before 2026-10-16 were recovered from the
git history of downloads.json, from the daily job's first sample (2026-10-08).

The output is byte-stable (sorted, fixed key order, trailing newline), so a day
with no new downloads leaves no git diff.
"""

import datetime
import json
import os
import re
import sys
import urllib.request

REPO = "thomas4f/mxbmrp3"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "usage_survey", "downloads.json")
HISTORY = os.path.join(os.path.dirname(HERE), "usage_survey", "downloads_history.csv")


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


def record(day, total, path=HISTORY):
    """Add `day`'s row unless the history already has one, keeping the rows
    date-sorted. Returns whether it was added: the first sample of a day stands
    (see the module header)."""
    rows = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f.read().splitlines()[1:]:
                d, n = line.split(",")
                rows[d] = int(n)
    if day in rows:
        return False
    rows[day] = total
    with open(path, "w") as f:
        f.write("date,downloads\n" + "".join(
            "{},{}\n".format(d, rows[d]) for d in sorted(rows)))
    return True


def refresh(data, day, out=OUT, history=HISTORY):
    """Write `data` and `day`'s history row, or neither when the day is already
    sampled: downloads.json feeds the Downloads tile and the release-line table,
    the history the bullet and the charts, and all of them must show one sample."""
    if not record(day, sum(x["downloads"] for x in data["releases"]), history):
        return False
    write(data, out)
    return True


def main():
    data = summarise(fetch())
    if not data["releases"]:
        sys.exit("error: no releases returned for {}".format(REPO))
    day = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    if not refresh(data, day):
        print("{} already sampled; left {} and {} as they are".format(day, OUT, HISTORY))
        return
    print("Wrote {}: {} releases, {:,} downloads".format(
        OUT, len(data["releases"]), sum(x["downloads"] for x in data["releases"])))


if __name__ == "__main__":
    main()
