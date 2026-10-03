#!/usr/bin/env python3
"""Pull Aptabase exports over HTTP, so the usage survey can regenerate unattended.

WHY THIS IS BESPOKE (see CLAUDE.md - "reach for standard tooling first"): Aptabase
ships no public query API, no CLI and no export SDK. Its own MCP server wraps the
dashboard's internal endpoints and explicitly declines bulk export. The dashboard's
`/api/_export/download` is therefore the only route to the raw events, and nothing
off the shelf speaks it. It is undocumented and may change without notice - the
validation below exists so that a change fails loudly instead of silently rewriting
history.

THREE PROPERTIES OF THAT ENDPOINT SHAPE THIS WHOLE FILE:

1. It answers HTTP 200 even when it failed. A Tinybird limit error arrives as a
   200 whose body is a ~230-byte JSON blob. Status codes are worthless here, so
   every response is validated by CONTENT.

2. It can truncate mid-stream, still at 200. The CSV path sets the status before
   streaming, then pages; if a page throws, `!Response.HasStarted` is already false
   so it cannot switch to a 500 - it just stops writing. A short CSV is
   indistinguishable from a complete one. Parquet cannot hide this: its PAR1 footer
   is written last, so a truncated file fails to open. That is the main reason
   parquet is the default here, ahead of it also being ~17x smaller.

3. Aptabase Cloud runs the query on Tinybird, which caps a result at 100 MiB. The
   parquet path issues ONE unpaginated query, so a fortnight (111.82 MiB) is already
   over the line. Hence one request per UTC day: ~1 MB and ~18k rows at current
   volume, far under both that cap and the CSV path's 100k-row pagination - which
   also means a day comes back as a single page, with no repeated header rows and
   no unstable `OFFSET` seam.

The fetched files feed `tools/analytics_report.py` unchanged; its `load()` already
de-duplicates overlapping exports, so re-fetching a day is always safe.

Credentials come from the environment, never the command line:
    APTABASE_AUTH_SESSION   the `auth-session` cookie (required)
    APTABASE_APP_ID         the app's NanoId - NOT the A-EU-… app key (required)
    APTABASE_HOST           default eu.aptabase.com
    APTABASE_APP_NAME       cosmetic; only shapes the server-side filename
"""

import argparse
import datetime as dt
import gzip
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_HOST = "eu.aptabase.com"
DEFAULT_ROLLUP = os.path.join(REPO_ROOT, "usage_survey", "rollup")
DEFAULT_OUT = os.path.join(REPO_ROOT, "usage_survey", "_exports")

# Deliberately NO minimum-size check: a genuinely quiet day returns a valid, tiny
# file, so size cannot separate "quiet day" from "broken response". Only the
# structural checks in validate() can, which is why they are the only gate.
PARQUET_MAGIC = b"PAR1"
RETRIES = 4
BACKOFF = 3.0


# ----------------------------------------------------------------------------
# Resume point
# ----------------------------------------------------------------------------

def resume_date(rollup_dir):
    """Newest UTC date the stored rollup already covers, or None if it is empty.

    Read from the shard rather than the manifest: the manifest records months
    ("2026-09"), which would re-fetch up to 30 days every run. The shards carry an
    `hour` column (YYYY-MM-DDTHH), so the real high-water mark is one max() away.

    Deriving the window from the rollup - rather than a fixed "last N days" - is
    what makes a missed run self-healing: the gap simply widens the next window
    instead of becoming a permanent hole, which is the failure the rollup exists
    to prevent in the first place.
    """
    manifest_path = os.path.join(rollup_dir, "manifest.json")
    if not os.path.exists(manifest_path):
        return None
    with open(manifest_path) as f:
        months = json.load(f).get("months", {})
    if not months:
        return None
    shard = os.path.join(rollup_dir, max(months) + ".csv.gz")
    if not os.path.exists(shard):
        raise SystemExit("error: manifest names {} but it is missing".format(shard))
    newest = None
    with gzip.open(shard, "rt") as f:
        header = f.readline().rstrip("\n").split(",")
        if "hour" not in header:
            raise SystemExit("error: {} has no 'hour' column".format(shard))
        col = header.index("hour")
        for line in f:
            parts = line.rstrip("\n").split(",")
            if len(parts) > col and (newest is None or parts[col] > newest):
                newest = parts[col]
    if not newest:
        return None
    return dt.datetime.strptime(newest[:10], "%Y-%m-%d").date()


def day_range(start, end):
    """Every UTC date in [start, end], inclusive."""
    days, cur = [], start
    while cur <= end:
        days.append(cur)
        cur += dt.timedelta(days=1)
    return days


# ----------------------------------------------------------------------------
# HTTP
# ----------------------------------------------------------------------------

def http_get(url, cookie, timeout=300):
    """GET with the session cookie, retrying only what is worth retrying.

    A 401/403 is a credential or share problem and will never fix itself, so it
    fails immediately rather than burning four attempts.
    """
    req = urllib.request.Request(url, headers={
        "Cookie": "auth-session=" + cookie,
        "Accept": "*/*",
        "User-Agent": "mxbmrp3-analytics-fetch/1",
    })
    last = None
    for attempt in range(RETRIES):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code in (401, 403):
                raise SystemExit(
                    "error: HTTP {} - the cookie is invalid/expired, or the app is not "
                    "shared with this account. Note the share is keyed on the EMAIL "
                    "string, so a '+' alias must match exactly.".format(e.code)) from None
            last = e
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            last = e
        if attempt < RETRIES - 1:
            time.sleep(BACKOFF * (2 ** attempt))
    raise SystemExit("error: giving up after {} attempts: {}".format(RETRIES, last))


def export_url(host, app_id, app_name, fmt, start, end):
    params = {
        "appId": app_id,
        "appName": app_name,
        "buildMode": "release",
        "format": fmt,
        "startDate": start,
        "endDate": end,
    }
    return "https://{}/api/_export/download?{}".format(host, urllib.parse.urlencode(params))


# ----------------------------------------------------------------------------
# Validation - the whole point of this script
# ----------------------------------------------------------------------------

def check_not_error(blob, what):
    """Reject the 200-with-an-error-body case before anything else looks at it."""
    head = blob[:200].lstrip()
    if head.startswith(b"{"):
        try:
            msg = json.loads(blob.decode("utf-8", "replace")).get("error", blob[:200])
        except ValueError:
            msg = blob[:200]
        raise SystemExit("error: {} returned an error body (HTTP 200): {}".format(what, msg))
    if head.lstrip().startswith(b"<"):
        raise SystemExit(
            "error: {} returned HTML - almost certainly a login page, meaning the "
            "cookie did not authenticate.".format(what))


def validate(blob, fmt, what):
    """Structural check. Parquet can prove it is complete; CSV can only look sane."""
    check_not_error(blob, what)
    if fmt == "parquet":
        if not (blob[:4] == PARQUET_MAGIC and blob[-4:] == PARQUET_MAGIC):
            raise SystemExit(
                "error: {} is not a complete parquet file (missing PAR1 magic at "
                "{}). The stream was almost certainly truncated mid-transfer.".format(
                    what, "start" if blob[:4] != PARQUET_MAGIC else "end"))
        return
    first = blob.split(b"\n", 1)[0].lower()
    if b"timestamp" not in first:
        raise SystemExit("error: {} has no CSV header (got {!r})".format(what, first[:80]))


def monthly_usage(host, app_id, cookie):
    """Per-month event totals, from a DIFFERENT query than the export.

    An independent expected-vs-actual figure is the only way to catch a short
    response that is nonetheless well-formed - the case parquet's footer cannot
    see, because a truncated *day* is still a valid file if the server simply
    returned fewer rows.
    """
    url = "https://{}/api/_export/usage?{}".format(
        host, urllib.parse.urlencode({"appId": app_id, "buildMode": "release"}))
    blob = http_get(url, cookie, timeout=60)
    check_not_error(blob, "/api/_export/usage")
    return {"{:04d}-{:02d}".format(r["year"], r["month"]): r["events"]
            for r in json.loads(blob.decode("utf-8"))}


# ----------------------------------------------------------------------------
# Self-test (no network, no pandas) - run by the `analytics-fetch-selftest` gate.
# ----------------------------------------------------------------------------

def selftest():
    """Exercise the resume logic and every rejection this script exists to make.

    The validation cases are the point. Each one is a real behaviour of the
    Aptabase export endpoint that answers HTTP 200, so a regression here would not
    fail loudly - it would quietly feed a short or bogus export into the rollup,
    which replaces the days it covers. That is silent, committed data loss, so
    each case is pinned rather than described.
    """
    import shutil
    import tempfile

    def rejects(what, *args):
        try:
            validate(*args)
        except SystemExit:
            return
        raise AssertionError("validate() accepted " + what)

    # --- the 200-with-an-error-body case (Tinybird result cap) ----------------
    rejects("a Tinybird error blob",
            b'{"error": "Limit for result exceeded, max bytes: 100.00 MiB"}',
            "parquet", "x")
    rejects("a Tinybird error blob as CSV", b'{"error": "nope"}', "csv", "x")

    # --- an unauthenticated response served as a login page -------------------
    rejects("an HTML login page", b"<!DOCTYPE html><html>", "csv", "x")

    # --- truncation: the failure CSV physically cannot show --------------------
    rejects("a parquet file with no footer", b"PAR1" + b"\x00" * 64, "parquet", "x")
    rejects("a parquet file with no header", b"\x00" * 64 + b"PAR1", "parquet", "x")
    rejects("a headerless CSV", b'"2026-09-01 00:00:00","abc"\n', "csv", "x")

    # --- and the shapes that must pass ----------------------------------------
    validate(b"PAR1" + b"\x00" * 64 + b"PAR1", "parquet", "ok")
    validate(b'"timestamp","user_id"\n"2026-09-01 00:00:00","a"\n', "csv", "ok")
    # A genuinely empty day is valid, not an error - do not let a size floor creep in.
    validate(b"PAR1" + b"\x00" * 8 + b"PAR1", "parquet", "empty day")

    # --- resume point comes from the shard's hour column, not the manifest -----
    tmp = tempfile.mkdtemp()
    try:
        with open(os.path.join(tmp, "manifest.json"), "w") as f:
            json.dump({"months": {"2026-08": {}, "2026-09": {}}}, f)
        with gzip.open(os.path.join(tmp, "2026-09.csv.gz"), "wt") as f:
            f.write("event,hour,install,n\n")
            f.write("app_started,2026-09-03T07,a,1\n")
            f.write("app_started,2026-09-11T22,b,2\n")   # the max, out of order
            f.write("app_started,2026-09-05T01,c,1\n")
        got = resume_date(tmp)
        assert got == dt.date(2026, 9, 11), got
        # Month granularity would have said 2026-09-01 and re-fetched 10 days.
        assert resume_date(tempfile.mkdtemp()) is None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # --- day_range is inclusive at both ends ----------------------------------
    assert len(day_range(dt.date(2026, 9, 1), dt.date(2026, 9, 1))) == 1
    assert len(day_range(dt.date(2026, 9, 1), dt.date(2026, 9, 30))) == 30

    # --- reconcile() buckets by MONTH, from the date object --------------------
    # Regression: verify() used to re-derive the date by splitting the filename
    # it had just built ("mxbmrp3-release-2026-09-11.parquet" -> "11"), so every
    # real run died in int("") after a wholly successful fetch. Caught only in
    # CI, because the selftest covered validation and resume but never this.
    sept = [(dt.date(2026, 9, 11), 100), (dt.date(2026, 9, 12), 50)]
    assert month_of(dt.date(2026, 9, 11)) == "2026-09"
    assert month_of(dt.date(2026, 12, 1)) == "2026-12"

    # A partial window is the daily case: reported, never compared, never fatal.
    out = reconcile(sept, dt.date(2026, 9, 11), dt.date(2026, 9, 12), {"2026-09": 9999})
    assert len(out) == 1 and "not checked" in out[0], out

    # A fully covered month IS compared - and a short one must be fatal, since
    # feeding it onward would overwrite good days in the rollup.
    full = [(dt.date(2026, 9, d), 10) for d in range(1, 31)]          # 300 rows
    whole = (dt.date(2026, 9, 1), dt.date(2026, 9, 30))
    out = reconcile(full, *whole, {"2026-09": 300})
    assert len(out) == 1 and "OK" in out[0], out
    reconcile(full, *whole, {"2026-09": 302})        # inside the 1%/50-row slack
    try:
        reconcile(full, *whole, {"2026-09": 5000})
    except SystemExit:
        pass
    else:
        raise AssertionError("reconcile() accepted a short month")

    # December must not wrap into year+1 month 13 when computing the last day.
    dec = [(dt.date(2026, 12, d), 1) for d in range(1, 32)]
    out = reconcile(dec, dt.date(2026, 12, 1), dt.date(2026, 12, 31), {"2026-12": 31})
    assert "OK" in out[0], out

    # --- the URL carries a whole UTC day, release build ------------------------
    url = export_url("eu.aptabase.com", "nano", "app", "parquet",
                     "2026-09-01T00:00:00Z", "2026-09-01T23:59:59Z")
    for want in ("appId=nano", "format=parquet", "buildMode=release",
                 "startDate=2026-09-01T00%3A00%3A00Z"):
        assert want in url, (want, url)

    print("analytics_fetch selftest: OK")
    return 0


# ----------------------------------------------------------------------------

def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser(
        description="Fetch Aptabase exports (one request per UTC day) for analytics_report.py.")
    ap.add_argument("--rollup", default=DEFAULT_ROLLUP,
                    help="rollup dir the resume point is read from (default: usage_survey/rollup)")
    ap.add_argument("--out-dir", default=DEFAULT_OUT,
                    help="where day files are written (default: usage_survey/_exports)")
    ap.add_argument("--format", choices=("parquet", "csv"), default="parquet",
                    help="parquet (default) is ~17x smaller AND detects truncation")
    ap.add_argument("--since", help="YYYY-MM-DD start, overriding the rollup resume point")
    ap.add_argument("--until", help="YYYY-MM-DD end (default: today, UTC)")
    ap.add_argument("--overlap-days", type=int, default=2,
                    help="re-fetch this many days before the resume point (default: 2). "
                         "load() de-dupes, so overlap is free insurance against a "
                         "partially-ingested boundary day.")
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the /api/_export/usage cross-check")
    args = ap.parse_args()

    cookie = os.environ.get("APTABASE_AUTH_SESSION", "").strip()
    app_id = os.environ.get("APTABASE_APP_ID", "").strip()
    if not cookie or not app_id:
        sys.exit("error: APTABASE_AUTH_SESSION and APTABASE_APP_ID must both be set")
    if app_id.startswith("A-"):
        sys.exit("error: APTABASE_APP_ID looks like the app KEY ({}). The export "
                 "endpoint matches on apps.id, a NanoId - read it from "
                 "/api/_apps.".format(app_id))
    host = os.environ.get("APTABASE_HOST", DEFAULT_HOST).strip() or DEFAULT_HOST
    app_name = os.environ.get("APTABASE_APP_NAME", "mxbmrp3").strip() or "mxbmrp3"

    today = dt.datetime.now(dt.timezone.utc).date()
    end = dt.datetime.strptime(args.until, "%Y-%m-%d").date() if args.until else today
    if args.since:
        start = dt.datetime.strptime(args.since, "%Y-%m-%d").date()
    else:
        resume = resume_date(args.rollup)
        if resume is None:
            sys.exit("error: no rollup at {} to resume from - pass --since for the "
                     "first run (or to rebuild).".format(args.rollup))
        start = resume - dt.timedelta(days=args.overlap_days)
    if start > end:
        print("Nothing to fetch: rollup already covers through {}.".format(end))
        return

    days = day_range(start, end)
    os.makedirs(args.out_dir, exist_ok=True)
    print("Fetching {} day(s), {} -> {}, as {} from {}".format(
        len(days), start, end, args.format, host))

    written, total = [], 0
    for day in days:
        name = "mxbmrp3-release-{}.{}".format(day, args.format)
        path = os.path.join(args.out_dir, name)
        url = export_url(host, app_id, app_name, args.format,
                         "{}T00:00:00Z".format(day), "{}T23:59:59Z".format(day))
        blob = http_get(url, cookie)
        validate(blob, args.format, name)
        with open(path, "wb") as f:
            f.write(blob)
        written.append((day, path))
        total += len(blob)
        print("  {}  {:>10,} bytes".format(day, len(blob)))

    print("Wrote {} file(s), {:,} bytes total, to {}".format(
        len(written), total, args.out_dir))

    if not args.no_verify:
        verify(host, app_id, cookie, start, end, written)

    return 0


def month_of(day):
    return "{:04d}-{:02d}".format(day.year, day.month)


def reconcile(day_rows, start, end, usage):
    """Compare fetched row counts against the usage endpoint's totals, per month.

    Pure, so the selftest can drive it without a network: the first version
    re-derived the date by splitting the filename it had just built, which
    bucketed 2026-09-11 under "11" and then died on int(""). Carrying the date
    object removes the round-trip that made that possible.

    Only a month the window FULLY covers is comparable - a partial window
    legitimately holds fewer events than the month's total, which is every daily
    run. Returns the lines to print; raises SystemExit on a short month.
    """
    counts = {}
    for day, n in day_rows:
        key = month_of(day)
        counts[key] = counts.get(key, 0) + n

    lines = []
    for month, got in sorted(counts.items()):
        year, mon = int(month[:4]), int(month[5:])
        first = dt.date(year, mon, 1)
        last = dt.date(year + (mon == 12), mon % 12 + 1, 1) - dt.timedelta(days=1)
        want = usage.get(month)
        if want is None or start > first or end < last:
            lines.append("verify: {} {:>9,} rows (partial window, not checked)".format(
                month, got))
            continue
        if abs(got - want) > max(50, want * 0.01):
            raise SystemExit(
                "error: {} fetched {:,} rows but Aptabase reports {:,} events. The "
                "export is short - refusing to feed a truncated month into the "
                "rollup, which would overwrite good days.".format(month, got, want))
        lines.append("verify: {} {:>9,} rows vs {:,} reported - OK".format(
            month, got, want))
    return lines


def verify(host, app_id, cookie, start, end, written):
    """Read back what was fetched and hand the counts to reconcile()."""
    try:
        import pandas as pd
    except ImportError:
        print("verify: pandas not available, skipping row-count cross-check")
        return
    usage = monthly_usage(host, app_id, cookie)
    day_rows = []
    for day, path in written:
        n = len(pd.read_parquet(path) if path.endswith(".parquet")
                else pd.read_csv(path, dtype=str, keep_default_na=False))
        day_rows.append((day, n))
    for line in reconcile(day_rows, start, end, usage):
        print(line)


if __name__ == "__main__":
    sys.exit(main())
