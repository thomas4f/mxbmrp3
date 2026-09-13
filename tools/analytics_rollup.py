#!/usr/bin/env python3
# ============================================================================
# tools/analytics_rollup.py
# The usage-survey report's MEMORY: a small, committed, de-identified digest of
# every export already processed, so a later run only needs the NEWEST export.
#
# WHY THIS EXISTS
#   Aptabase exports one month at a time and the raw exports are deliberately
#   not kept in the repo (they are ~100-280 MB each). Regenerating the report
#   therefore meant re-supplying every month since reporting began, and the
#   window silently shrank to whatever was on hand -- a run with only the
#   newest export turned a 73-day report into a 7-day one, with no error.
#   This module writes the digest back to usage_survey/rollup/ after each run and
#   reads it on the next, so the window only ever grows.
#
# WHAT IS STORED, AND WHY IT IS NOT "THE RAW EXPORT, ZIPPED"
#   Every figure in the report is an aggregate, so the digest keeps only what
#   an aggregate can be rebuilt from:
#     - launches   as (hour, install, game, version, coverage-bits) + a repeat
#                  count. Hour, not timestamp: collection_gaps() resamples
#                  hourly and nothing in the report is finer.
#     - sessions   as (hour, version, duration) + count.
#     - crashes    as (hour, install, game, + the fault fields) + count.
#     - everything else (analytics_disabled, link_clicked, app_ended) as its
#                  hour and identity alone -- they carry no payload the report
#                  reads, but they ARE part of the hourly volume and the
#                  per-install event totals.
#     - installs   one row per install: its LATEST app_started payload, which
#                  is the whole of `snap` (features, HUDs, achievements, ...).
#   Everything else the export carries -- user_id, session_id, locale, region,
#   the per-launch props of every launch that is not an install's latest -- is
#   dropped, because no metric reads it. That is the compression: ~650 MB of
#   CSV becomes ~1.5 MB, not by compressing harder but by storing less.
#
#   `install_id` is REPLACED by a blake2s digest of itself (iid()), so the
#   committed files carry no id that appears in a user's own data folder. The
#   hash is stable across runs, which is what lets a later export be merged
#   into the same install's history; DEV_IDS are hashed the same way so the
#   dev-install exclusion keeps working.
#
# ROUND-TRIP CONTRACT
#   read() returns frames shaped exactly like load()'s, so build() cannot tell
#   a rolled-up month from a freshly-read export. The selftest asserts that
#   directly: the same fixture through raw-load and through a write/read cycle
#   must produce the same report. Where a stored row cannot carry a field
#   (a historical launch has no props), the report reads that fact from a
#   column instead -- see COV_* below and _coverage() in analytics_report.py.
#
# FORMAT
#   usage_survey/rollup/manifest.json   schema version + per-month row counts
#   usage_survey/rollup/<YYYY-MM>.csv.gz  one month of launches/sessions/crashes
#   usage_survey/rollup/installs.csv.gz   one row per install (its latest ping)
#   Text CSV, gzipped with mtime=0 and rows sorted, so a month whose data has
#   not changed re-writes BYTE-IDENTICALLY and git sees no diff -- AFTER its
#   first round trip. A month written straight from a raw export compacts once
#   when it is next read back and rewritten: expansion normalises timestamps to
#   this file's own hour+seq resolution, so two launches the raw second-level
#   order kept in separate runs become one row with a higher `n`. Same events,
#   fewer rows, same report (verified against the raw exports); it converges
#   after that one pass, so a month churns in git exactly once.
# ============================================================================
import gzip
import hashlib
import io
import json
import os
import re

import pandas as pd

SCHEMA = 1

# Which fields a launch was able to report. Stored as a bitmask because the
# props themselves are not kept for historical launches, and the "About this
# data" coverage table is the one place that asks the question per launch
# rather than per install.
COV_FEATURES = 1
COV_HUDS = 2
COV_OS = 4
COV_CHANNEL = 8
COV_THEME = 16

# The event name is stored verbatim rather than mapped to a code, so an event kind
# the report does not read today (analytics_disabled, link_clicked, app_ended) still
# round-trips. They are a rounding error in volume and NOT in meaning: they are in the
# hourly counts collection_gaps() reads, and in the per-install event totals the
# developer-exclusion note quotes.
EVENT_SESSION = "session_end"
EVENT_CRASH = "crash"

# Crash fields lifted out of string_props. Kept in this order in the shard.
CRASH_FIELDS = ["host", "fault", "code", "av_type", "game_build", "crash_plugin_version"]

_MONTH_COLS = (["event", "hour", "install", "game", "version", "cov", "secs"]
               + CRASH_FIELDS + ["seq", "n"])

# Columns that IDENTIFY a digest row. `seq` and `n` describe it instead: seq orders an
# install's rows inside one hour, n counts the repeats the row stands for.
_GROUP_COLS = [c for c in _MONTH_COLS if c not in ("seq", "n")]

# The columns `snap` is read through, and the only ones installs.csv.gz round-trips.
# merge_installs() projects both sides onto them so a stored row and a fresh one are
# the same shape: concat'ing frames with different columns leaves NaNs that
# groupby().last() then fills from the OTHER row, silently mixing two installs' worth
# of values into one snapshot.
SNAP_COLS = ["install_id", "timestamp", "ts", "date", "event_name", "game",
             "app_version", "country_name", "os_version", "cov", "_s", "_n"]


def iid(raw):
    """Stable, one-way id for an install. 16 hex chars of blake2s: collision
    odds across even a million installs are ~1e-7, and nothing here needs to
    go back the other way."""
    if not raw:
        return ""
    return hashlib.blake2s(str(raw).encode("utf-8"), digest_size=8).hexdigest()


def coverage_bits(s, n, os_version):
    """The COV_* mask for one app_started row, from its own props."""
    bits = 0
    if any(k.startswith("feat_") for k in n):
        bits |= COV_FEATURES
    if any(k.startswith(("hud_", "widget_")) for k in n):
        bits |= COV_HUDS
    if os_version:
        bits |= COV_OS
    if "update_channel" in s:
        bits |= COV_CHANNEL
    if "panel_theme" in s:
        bits |= COV_THEME
    return bits


# ----------------------------------------------------------------------------
# Writing
# ----------------------------------------------------------------------------


def _write_gz(path, text):
    """Deterministic gzip: no mtime, no filename, so identical content on two
    runs is an identical FILE and the commit shows no diff for a month nothing
    happened in."""
    buf = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=buf, mtime=0) as gz:
        gz.write(text.encode("utf-8"))
    with open(path, "wb") as f:
        f.write(buf.getvalue())


def _month_rows(df):
    """The event digest: one row per distinct (event, hour, identity, payload),
    with `n` counting the repeats it stands for."""
    keep = df.copy()
    if keep.empty:
        return pd.DataFrame(columns=_MONTH_COLS)
    keep["event"] = keep["event_name"]
    keep["hour"] = keep["ts"].dt.strftime("%Y-%m-%dT%H")
    keep["install"] = keep["install_id"].fillna("")
    keep["version"] = keep["app_version"].fillna("")
    keep["cov"] = keep["cov"].fillna(0).astype("int64")
    keep["secs"] = [
        (n.get("duration_seconds") if e == EVENT_SESSION else None)
        for e, n in zip(keep["event"], keep["_n"])
    ]
    for field in CRASH_FIELDS:
        keep[field] = [
            (s.get(field) if e == EVENT_CRASH else None)
            for e, s in zip(keep["event"], keep["_s"])
        ]
    group = _GROUP_COLS
    keep = keep.fillna({c: "" for c in group}).sort_values("ts", kind="stable")
    # WITHIN-HOUR ORDER, kept because one metric depends on it: the version-migration
    # chart credits each install to the last version it ran that DAY, so two launches an
    # hour apart-or-less must keep their order. `seq` numbers the RUNS of identical rows
    # an install has inside one hour, which is stronger than numbering the distinct rows
    # and is the difference between right and wrong for a real case: three installs in
    # this data ran A, then B, then A again within the hour (someone switching builds),
    # and a per-row number ends the hour on B. Consecutive identical launches still
    # collapse into one row, so seq is 0 almost everywhere and costs nothing compressed.
    gid = keep.groupby(group, sort=False, observed=True).ngroup()
    same = (gid.eq(gid.shift())
            & keep["install"].eq(keep["install"].shift())
            & keep["hour"].eq(keep["hour"].shift()))
    keep["seq"] = (~same).groupby([keep["install"], keep["hour"]]).cumsum().astype("int64") - 1
    out = (keep.groupby(group + ["seq"], dropna=False, observed=True)
               .size().reset_index(name="n"))
    # SORTED BY INSTALL FIRST, and that is a size decision, not a cosmetic one: it puts
    # an install's ~50 rows next to each other, where gzip's window can collapse the
    # 16-hex id (and the repeated game/version) instead of meeting it fresh every few
    # hundred rows. Measured on the 2026-08 shard: 2.34 MB by (event, hour) against
    # 1.33 MB by (install, event, hour), for identical content. The COLUMN order stays
    # _MONTH_COLS so the file still reads left-to-right as event/when/who.
    sort_by = ["install", "event", "hour"] + [c for c in group
                                              if c not in ("install", "event", "hour")]
    sort_by.append("seq")   # the one key that can still tie: the same row in two runs
    return out.sort_values(sort_by, kind="stable")[_MONTH_COLS]


def _install_rows(snap):
    """One row per install: identity, the last-seen scalars, and its latest
    props. String props stay a JSON blob (a few short keys, and "" is a
    legitimate VALUE there, which a CSV cell could not tell from absent);
    numeric props are spread one-per-column, where an empty cell unambiguously
    means the build did not send that key -- which is exactly what every
    coverage-aware percentage in the report divides by."""
    keys = sorted({k for n in snap["_n"] for k in n})
    rows = []
    for _, row in snap.iterrows():
        rec = {
            "install": row["install_id"],
            # Normalised from `ts`, never echoed from the export: CSV exports carry
            # "YYYY-MM-DD HH:MM:SS" but parquet carries epoch seconds, and a file
            # holding both shapes cannot be sorted or re-parsed.
            "timestamp": row["ts"].strftime("%Y-%m-%d %H:%M:%S"),
            "game": row["game"],
            "version": row["app_version"],
            "country": row["country_name"],
            "os_version": row["os_version"],
            "s": json.dumps(row["_s"], sort_keys=True, separators=(",", ":")),
        }
        for k, v in row["_n"].items():
            rec["n:" + k] = v
        rows.append(rec)
    cols = ["install", "timestamp", "game", "version", "country", "os_version", "s"] \
        + ["n:" + k for k in keys]
    return pd.DataFrame(rows, columns=cols).sort_values("install", kind="stable")


def write(rollup_dir, df, snap):
    """Persist `df` (all events, post-merge) and `snap` (one row per install)."""
    os.makedirs(rollup_dir, exist_ok=True)
    months = _month_rows(df)
    counts = {}
    if len(months):
        months["_m"] = months["hour"].str.slice(0, 7)
        for month, sub in months.groupby("_m"):
            sub = sub.drop(columns=["_m"])
            _write_gz(os.path.join(rollup_dir, month + ".csv.gz"),
                      sub.to_csv(index=False))
            counts[month] = {"rows": int(len(sub)), "events": int(sub["n"].sum())}
    _write_gz(os.path.join(rollup_dir, "installs.csv.gz"),
              _install_rows(snap).to_csv(index=False))
    # A month that no longer has any data (an export withdrawn, a rebuild from a
    # shorter set) leaves its shard behind otherwise: read() goes by the manifest, so
    # the file would be invisible AND committed.
    for stale in os.listdir(rollup_dir):
        if re.fullmatch(r"\d{4}-\d{2}\.csv\.gz", stale) and stale[:-7] not in counts:
            os.remove(os.path.join(rollup_dir, stale))
    manifest = {
        "schema": SCHEMA,
        "note": "Digest of processed Aptabase exports; see tools/analytics_rollup.py. "
                "Delete this directory to rebuild from raw exports.",
        "window": {"first": min(counts) if counts else None,
                   "last": max(counts) if counts else None},
        "installs": int(len(snap)),
        "months": counts,
    }
    with open(os.path.join(rollup_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    return manifest


# ----------------------------------------------------------------------------
# Reading
# ----------------------------------------------------------------------------


def _expand(months):
    """Digest rows back into one row per event, shaped like load()'s output.

    `n` is undone here rather than carried as a weight: every metric in the
    report is a count, a nunique or a median over rows, and teaching each of
    them about weights is a far larger surface than materialising 700k small
    rows once."""
    if not len(months):
        return None
    df = months.loc[months.index.repeat(months["n"].astype(int))].reset_index(drop=True)
    df["event_name"] = df["event"]
    df["ts"] = (pd.to_datetime(df["hour"], format="%Y-%m-%dT%H", utc=True)
                + pd.to_timedelta(pd.to_numeric(df["seq"], errors="coerce").fillna(0),
                                  unit="s"))
    df["timestamp"] = df["ts"].dt.strftime("%Y-%m-%d %H:%M:%S")
    df["date"] = df["ts"].dt.date
    # BACK TO None, not "": a handful of events carry no install_id at all, and an
    # empty string is a value -- it counts as one more install in every nunique() in
    # the report, which is exactly the kind of off-by-one nobody would trace back here.
    df["install_id"] = df["install"].replace("", None)
    df["app_version"] = df["version"]
    df["game"] = df["game"].fillna("Unknown").replace("", "Unknown")
    df["cov"] = pd.to_numeric(df["cov"], errors="coerce").fillna(0).astype("int64")
    df["os_version"] = ""
    df["country_name"] = ""
    # Sessions and crashes are re-given the props their readers destructure, so
    # crash_population() and the session-length code stay ignorant of the rollup.
    # A launch gets an EMPTY payload on purpose: its props were not stored (only
    # the latest per install were), and an empty dict is the honest shape for
    # "this launch reported nothing we kept" -- the coverage table reads `cov`.
    secs = pd.to_numeric(df["secs"], errors="coerce")
    df["_n"] = [{"duration_seconds": float(v)} if e == EVENT_SESSION and pd.notna(v) else {}
                for e, v in zip(df["event"], secs)]
    crash_cols = [df[f].fillna("").tolist() for f in CRASH_FIELDS]
    df["_s"] = [
        ({f: vals[i] for f, vals in zip(CRASH_FIELDS, crash_cols) if vals[i] != ""}
         if e == EVENT_CRASH else {})
        for i, e in enumerate(df["event"])
    ]
    df["hist"] = True
    cols = ["timestamp", "ts", "date", "event_name", "install_id", "game",
            "app_version", "os_version", "country_name", "cov", "_s", "_n", "hist"]
    return df[cols]


def _read_installs(path):
    """installs.csv.gz -> a frame shaped exactly like latest_per_install()'s."""
    raw = pd.read_csv(path, dtype=str, keep_default_na=False, compression="gzip")
    if not len(raw):
        return None
    ncols = [c for c in raw.columns if c.startswith("n:")]
    nvals = {c: pd.to_numeric(raw[c], errors="coerce") for c in ncols}
    out = pd.DataFrame({
        "install_id": raw["install"],
        "timestamp": raw["timestamp"],
        "game": raw["game"],
        "app_version": raw["version"],
        "country_name": raw["country"],
        "os_version": raw["os_version"],
        "event_name": "app_started",
    })
    out["ts"] = pd.to_datetime(out["timestamp"], utc=True, errors="coerce")
    out["date"] = out["ts"].dt.date
    out["_s"] = [json.loads(v) if v else {} for v in raw["s"]]
    # An EMPTY CELL IS AN ABSENT KEY, not a zero: "78% of installs report Windows 11"
    # divides by the installs that sent the field, and a build predating a flag must
    # stay out of that denominator rather than count as "feature off".
    out["_n"] = [
        {c[2:]: (int(v) if float(v).is_integer() else float(v))
         for c in ncols for v in [nvals[c].iloc[i]] if pd.notna(v)}
        for i in range(len(raw))
    ]
    out["cov"] = [coverage_bits(s, n, o)
                  for s, n, o in zip(out["_s"], out["_n"], out["os_version"])]
    return out


def read(rollup_dir):
    """Load a previously written rollup.

    Returns (events, installs) as load()-shaped frames, or (None, None) when
    there is nothing stored yet."""
    manifest_path = os.path.join(rollup_dir, "manifest.json")
    if not os.path.exists(manifest_path):
        return None, None
    with open(manifest_path) as f:
        manifest = json.load(f)
    if manifest.get("schema") != SCHEMA:
        raise SystemExit(
            "error: {} is schema {}, this tool writes {}. Delete {} and re-run with "
            "every export to rebuild it.".format(manifest_path, manifest.get("schema"),
                                                 SCHEMA, rollup_dir))
    frames = []
    for month in sorted(manifest.get("months", {})):
        path = os.path.join(rollup_dir, month + ".csv.gz")
        if not os.path.exists(path):
            raise SystemExit("error: {} names {} but the file is missing".format(
                manifest_path, os.path.basename(path)))
        frames.append(pd.read_csv(path, dtype=str, keep_default_na=False,
                                  compression="gzip"))
    events = _expand(pd.concat(frames, ignore_index=True)) if frames else None
    ipath = os.path.join(rollup_dir, "installs.csv.gz")
    installs = _read_installs(ipath) if os.path.exists(ipath) else None
    return events, installs


# ----------------------------------------------------------------------------
# Merging
# ----------------------------------------------------------------------------


def merge_events(hist, fresh):
    """Stored history + a freshly-read export, with the export authoritative.

    A DAY the export covers is dropped wholesale from the history rather than
    de-duplicated row by row: exports are month-aligned, so the only overlap in
    practice is a re-run of the same month, and re-reading it must not double
    every launch in it. Dropping by day also lets a re-export FIX a day (a
    partial pull, a quota gap that back-filled) instead of merging with it."""
    if hist is None or not len(hist):
        return fresh
    if fresh is None or not len(fresh):
        return hist
    covered = set(fresh["date"].unique())
    kept = hist[~hist["date"].isin(covered)]
    return pd.concat([kept, fresh], ignore_index=True)


def merge_installs(hist, fresh):
    """One row per install, the later `timestamp` winning.

    An install dormant since July keeps the July snapshot -- that IS its current
    configuration as far as anyone knows -- while one that launched today is
    updated. Same rule the report already applies within a single window
    ("its most recently seen values"), extended across windows."""
    if hist is None or not len(hist):
        return fresh
    if fresh is None or not len(fresh):
        return hist
    both = pd.concat([hist[SNAP_COLS], fresh[SNAP_COLS]], ignore_index=True)
    # By `ts`, not `timestamp`: ts is a real datetime on both sides, while the raw
    # timestamp is a string from a CSV export and an integer from a parquet one.
    both = both.sort_values("ts", kind="stable")
    # Whole rows, for the reason given at latest_per_install(): groupby().last() is
    # per-column, so a fresh row with a null field would silently inherit the stored
    # one and the merged snapshot would belong to neither ping.
    return both.drop_duplicates("install_id", keep="last")
