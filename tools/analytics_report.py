#!/usr/bin/env python3
# ============================================================================
# tools/analytics_report.py
# Turn Aptabase monthly exports into a static Markdown dashboard + SVG charts,
# checked into analytics/ (the raw exports are NOT kept in the repo).
#
#   python3 tools/analytics_report.py <export1.csv> [<export2.parquet> ...]
#   python3 tools/analytics_report.py path/to/exports/*.csv --out analytics
#
# EXPORT FORMAT: .csv or .parquet, and they mix freely in one run — the column
# schema is identical either way. The only real difference is `timestamp`: parquet carries
# epoch seconds, CSV carries "YYYY-MM-DD HH:MM:SS". See read_export()/to_utc().
#
# The MXBMRP3 plugin emits Aptabase events (app_started / session_end / crash /
# app_ended / link_clicked / analytics_disabled). Aptabase ingests them well but
# its dashboards don't show what a plugin dev / users / the upstream game dev
# actually care about, so this tool re-derives the metrics we want.
#
# WHY install_id AND NOT user_id:
#   Aptabase's `user_id` is a privacy-preserving daily-rotating hash (~8 distinct
#   ids per real install in this data), so it OVER-counts installs badly. The
#   plugin sends its own stable `install_id` in string_props -- that is the real
#   unique-install identity, and every install-level metric here keys on it.
#
# SCHEMA EVOLUTION (the "telemetry added in 1.26, refined in 1.27" caveat):
#   Early builds (notably 1.26.0.0) send a MINIMAL payload -- no os_version, no
#   locale, and none of the feat_*/hud_*/widget_* flags. Rather than silently
#   averaging over a denominator that doesn't include those events, every
#   feature/geo/OS metric is COVERAGE-AWARE: it reports the value AND the number
#   of installs that actually reported the field, so a partial rollout can't be
#   mistaken for "nobody uses it". See the "Data coverage" section of the report.
#
# Dependencies: pandas; pyarrow only if you feed it parquet. Dev-only -- see
# tools/requirements.txt.
# Sibling module tools/analytics_svg.py holds the (dependency-free) SVG charts.
# ============================================================================
import argparse
import glob
import json
import os
import re
import sys
from collections import Counter, defaultdict

try:
    import pandas as pd
except ImportError:
    sys.exit("error: pandas is required (pip install -r tools/requirements.txt)")

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analytics_svg as svg  # noqa: E402

REPO_ROOT = os.path.dirname(HERE)
KNOWN_CRASHES = os.path.join(REPO_ROOT, "crash_analysis", "known_game_crashes.json")

# Hosts that are NOT a player running the game -- dev/replay tooling. Their
# crashes must not pollute the player-facing crash rate.
DEV_HOSTS = {"mxbmrp3_replay.exe", "mxbmrp3_hud_window.exe", "mxbmrp3_fontgen.exe"}

# Developer / test install_ids to drop report-wide. These are the plugin author's
# own machines: they rack up hundreds of launches across every dev build number
# and deliberately trigger crashes to validate the telemetry pipeline, which
# would otherwise show up as a phantom "plugin crash" cluster. Add IDs here; the
# game's own dev-tool hosts are handled separately by DEV_HOSTS.
DEV_INSTALL_IDS = {
    "d7983bfc-166e-457b-9be5-60e1d8c33c49",  # author - MX Bikes
    "e44bd23d-4e50-40d0-9662-9398f7e9d4fe",  # author - GP Bikes
    "8b10ae0d-ec97-4807-b51b-4e6802d51fa5",  # author - Kart Racing Pro
}

# Human labels for the stable feat_* flags (from analytics_manager.cpp). Unknown
# flags still render, prettified generically, so new features aren't dropped.
FEATURE_LABELS = {
    "feat_steam": "Steam friends",
    "feat_overlay": "Web overlay (OBS)",
    "feat_rumble": "Controller rumble",
    "feat_helmet": "Helmet overlay",
    "feat_director": "Auto-director",
    "feat_autoswitch": "Profile auto-switch",
    "feat_updates": "Update checker",
    "feat_widgets": "Widgets (master)",
    "feat_companion": "Companion window",
    "feat_thread": "Worker thread",
    # The two renderer flags are easy to confuse and are NOT the same window:
    # hwAccel is D3D11 in the companion window, glInGame is the plugin drawing the
    # IN-GAME HUD in the game's own GL context. Both report the setting, not the
    # backend that came up (see the SDK notes in analytics_manager.cpp).
    "feat_hwaccel": "GPU rendering (companion)",
    "feat_glingame": "Direct GL (in-game HUD)",
    "feat_devmode": "Developer mode",
    "feat_discord": "Discord presence",
}

# ----------------------------------------------------------------------------
# Loading & normalisation
# ----------------------------------------------------------------------------


_ACRONYMS = {"Fmx": "FMX", "Ecu": "ECU", "G Force": "G-Force", "Hud": "HUD"}


def _pretty_key(key):
    """hud_lap_log -> 'Lap Log' ; widget_g_force -> 'G-Force' ; hud_fmx -> 'FMX'."""
    for pre in ("hud_", "widget_", "feat_"):
        if key.startswith(pre):
            key = key[len(pre):]
            break
    label = key.replace("_", " ").title()
    return _ACRONYMS.get(label, label)


def read_export(path):
    """Read one Aptabase export. Parquet or CSV — Aptabase has offered both.

    CSV is read as all-strings with NA disabled so the columns behave exactly like
    the parquet ones: this code treats "missing" as the empty string throughout
    (`.replace("", pd.NA)`), and pandas' default NaN coercion would break that.
    """
    if os.path.splitext(path)[1].lower() != ".csv":
        return pd.read_parquet(path)
    df = pd.read_csv(path, dtype=str, keep_default_na=False)
    # Aptabase's CSV export concatenates paginated chunks and REPEATS the header row
    # between them (2 such rows in the 228k-row 2026-07 export). Left in, they parse
    # as un-dated events and blow up much later, in a date reduction, with a
    # TypeError that says nothing about the cause. Drop them at the source.
    if "timestamp" in df.columns:
        df = df[df["timestamp"] != "timestamp"].reset_index(drop=True)
    return df


def to_utc(ts):
    """Normalize a `timestamp` column to tz-aware UTC across export formats.

    Parquet carries epoch seconds (or a real datetime); CSV carries
    "YYYY-MM-DD HH:MM:SS" strings. Dispatch on the dtype rather than guessing,
    so a format change surfaces as bad dates rather than a crash.
    """
    if pd.api.types.is_datetime64_any_dtype(ts):
        return pd.to_datetime(ts, utc=True)
    if pd.api.types.is_numeric_dtype(ts):
        return pd.to_datetime(ts, unit="s", utc=True)
    return pd.to_datetime(ts, utc=True, errors="coerce")


def load(paths):
    frames = []
    for p in paths:
        try:
            frames.append(read_export(p))
        except Exception as e:  # noqa: BLE001
            sys.exit("error: failed to read {}: {}".format(p, e))
    df = pd.concat(frames, ignore_index=True)
    # Same event can appear in overlapping exports -- drop exact dupes.
    df = df.drop_duplicates(
        subset=["timestamp", "user_id", "session_id", "event_name",
                "string_props", "numeric_props"]
    ).reset_index(drop=True)

    def parse(col):
        out = []
        for v in df[col]:
            try:
                d = json.loads(v) if v else {}
                out.append(d if isinstance(d, dict) else {})
            except Exception:  # noqa: BLE001
                out.append({})
        return out

    df["_s"] = parse("string_props")
    df["_n"] = parse("numeric_props")
    df["install_id"] = [s.get("install_id") for s in df["_s"]]
    df["game"] = [s.get("game") or "Unknown" for s in df["_s"]]
    df["ts"] = to_utc(df["timestamp"])
    # Anything still undated is a malformed row. Drop it loudly rather than letting a
    # NaT propagate into `date` and surface as an unrelated TypeError in a min()/max()
    # reduction hundreds of lines away.
    undated = int(df["ts"].isna().sum())
    if undated:
        print("warning: dropped {} row(s) with an unparseable timestamp".format(undated),
              file=sys.stderr)
        df = df[df["ts"].notna()].reset_index(drop=True)
    df["date"] = df["ts"].dt.date
    return df


def latest_per_install(started):
    """One row per install: its most recent app_started (current config snapshot)."""
    s = started.sort_values("timestamp")
    s = s[s["install_id"].notna()]
    return s.groupby("install_id", as_index=False).last()


# ----------------------------------------------------------------------------
# Coverage-aware feature aggregation
# ----------------------------------------------------------------------------


# Numeric summary keys that share a flag prefix but are NOT 0/1 adoption flags.
_NOT_FLAGS = {"hud_count", "widget_count"}


def flag_adoption(snap, prefix):
    """For every '<prefix>*' numeric flag, return
    [(key, enabled_installs, reporting_installs)], sorted by adoption %.
    Denominator is installs that actually SENT the flag (coverage-aware)."""
    enabled = Counter()
    reporting = Counter()
    for n in snap["_n"]:
        for k, v in n.items():
            if k.startswith(prefix) and k not in _NOT_FLAGS:
                reporting[k] += 1
                if v:
                    enabled[k] += 1
    rows = [(k, enabled[k], reporting[k]) for k in reporting]
    rows.sort(key=lambda r: (r[1] / r[2] if r[2] else 0, r[1]), reverse=True)
    return rows


# ----------------------------------------------------------------------------
# Crash analysis
# ----------------------------------------------------------------------------

MODULE_CATEGORY = [
    # (predicate on lowercased module, category)
    (lambda m: m in ("mxbikes.exe", "gpbikes.exe", "kart.exe", "wrs.exe"), "Game"),
    (lambda m: m.startswith("mxbmrp3") or m.startswith("wrsmrp3"), "Plugin (MXBMRP3)"),
    (lambda m: "gameoverlay" in m or "discordhook" in m or m.startswith("obs")
     or "rtsshooks" in m or "overlay" in m or "gamebar" in m, "Overlay / capture"),
    (lambda m: m.startswith(("nvogl", "nvd3d", "nvcuda", "ig", "atio", "amdvlk",
                             "vulkan", "opengl32")), "Graphics driver"),
    (lambda m: m.startswith(("msvcr", "ucrtbase", "vcruntime", "msvcp", "ntdll",
                             "kernel", "combase", "ole32", "user32", "gdi32",
                             "win32u")), "System / runtime"),
]


def categorize_module(module):
    m = (module or "").lower()
    if not m or m == "unknown":
        return "Unknown"
    for pred, cat in MODULE_CATEGORY:
        if pred(m):
            return cat
    return "Other / third-party"


def load_known_crashes():
    """Return (build->pretty, lookup) where lookup maps ('<key>','+<off>') ->
    crash dict. key is a game_build hash for game-module faults, or a lowercased
    module name for system-module faults (matching the registry's `builds`)."""
    if not os.path.exists(KNOWN_CRASHES):
        return {}, {}
    reg = json.load(open(KNOWN_CRASHES))
    lookup = {}
    for c in reg.get("crashes", []):
        for bk, off in (c.get("builds") or {}).items():
            # Lowercase both build-hash and module keys so the join is
            # case-insensitive (the plugin emits an uppercase 0x… build hash today,
            # but a lowercase registry entry must not silently drop all its matches).
            lookup[(bk.lower(), off.lower())] = c
    return reg.get("build_versions", {}), lookup


def match_known(fault, game_build, lookup):
    if not fault or "+" not in fault:
        return None
    module, off = fault.split("+", 1)
    off = ("+" + off).lower()
    m = module.lower()
    if m in ("mxbikes.exe", "gpbikes.exe", "kart.exe", "wrs.exe"):
        return lookup.get(((game_build or "").lower(), off))
    return lookup.get((m, off))


# ----------------------------------------------------------------------------
# Report building
# ----------------------------------------------------------------------------


class Report:
    def __init__(self, out_dir):
        self.out = out_dir
        self.charts = os.path.join(out_dir, "charts")
        os.makedirs(self.charts, exist_ok=True)
        # Drop stale charts from a previous run so removed charts don't linger.
        for f in glob.glob(os.path.join(self.charts, "*.svg")):
            os.remove(f)
        self.md = []
        self._chart_names = set()

    def w(self, *lines):
        self.md.extend(lines)

    def chart(self, name, svg_text, alt):
        assert name not in self._chart_names, "duplicate chart " + name
        self._chart_names.add(name)
        with open(os.path.join(self.charts, name), "w") as f:
            f.write(svg_text)
        self.md.append("![{}](charts/{})".format(alt, name))
        self.md.append("")

    def save(self):
        path = os.path.join(self.out, "REPORT.md")
        with open(path, "w") as f:
            f.write("\n".join(self.md).rstrip() + "\n")
        return path


def pct(a, b):
    return (100.0 * a / b) if b else 0.0


def pctstr(count, total):
    """'46%', '<1%' for a nonzero share below 1%, '>99%' for a share above 99%
    that isn't the whole (so '100%' only ever means literally all)."""
    p = pct(count, total)
    if count > 0 and p < 1.0:
        return "<1%"
    if count < total and p > 99.0:
        return ">99%"
    return "{:.0f}%".format(p)


def cp(count, total):
    """'1,606 (46%)' - count with its share, the convention for count charts."""
    return "{:,} ({})".format(int(count), pctstr(count, total))


def pc(count, total):
    """'46% (1,606)' - share with its count, the convention for adoption charts."""
    return "{} ({:,})".format(pctstr(count, total), int(count))


def ver_family(v):
    return ".".join(str(v).split(".")[:2])


def ver_ge(v, *minimum):
    """True if version string v is >= the given (major, minor[, patch]) tuple."""
    parts = str(v).split(".")
    try:
        return tuple(int(parts[i]) for i in range(len(minimum))) >= tuple(minimum)
    except (ValueError, IndexError):
        return False


# Crash telemetry only became fully instrumented at plugin 1.27.5: that release
# added the faulting-thread backtrace (stack) and the access-violation type on
# top of the crash_plugin_version + game_build pinned in 1.27.0. Earlier builds
# under-reported crashes and carry no stack/av_type, so all crash stats are
# computed over 1.27.5+ only, where the whole crash population is consistent.
CRASH_MIN = (1, 27, 5)
CRASH_MIN_STR = ".".join(map(str, CRASH_MIN))


def build(df, out_dir):
    r = Report(out_dir)
    # Drop developer/test installs report-wide before deriving anything.
    is_dev = df["install_id"].isin(DEV_INSTALL_IDS)
    dev_installs = int(df.loc[is_dev, "install_id"].nunique())
    dev_events = int(is_dev.sum())
    df = df[~is_dev].copy()

    started = df[df.event_name == "app_started"].copy()
    sessions = df[df.event_name == "session_end"].copy()
    crashes = df[df.event_name == "crash"].copy()
    snap = latest_per_install(started)  # one row per install (current config)

    d0, d1 = df["date"].min(), df["date"].max()
    n_days = (d1 - d0).days + 1
    installs = snap["install_id"].nunique()

    # ---- Header + summary tiles ------------------------------------------
    r.w("# MXBMRP3 - Analytics Report", "")
    r.w("<!-- GENERATED by tools/analytics_report.py from Aptabase exports. "
        "Do not edit by hand; re-run the tool. Raw exports are not kept in the repo. -->", "")
    # The window states its COVERAGE, not just its ends. A reader comparing two
    # reports needs to know the denominator changed when a quota outage ate eleven
    # days of one of them; the gaps themselves are named in Activity over time.
    axis = day_axis(df)
    window = "**Data window:** {} → {} ({} days".format(d0, d1, n_days)
    window += ", {} observed)".format(len(axis["observed"])) if axis["gaps"] else ")"
    r.w(window, "")
    # Reach only. Deliberately NOT games (always ~3, and the Games section breaks it
    # down anyway) and NOT a raw crash-report count -- that number is meaningless
    # without the denominator and the 1.27.5+ instrumentation caveat, both of which
    # the Crashes section carries. A scary total up top invites the wrong reading.
    tiles = [
        ("Installs", "{:,}".format(installs)),
        ("Launches", "{:,}".format(len(started))),
        ("Countries", str(snap["country_name"].replace("", pd.NA).nunique())),
    ]
    r.w("| " + " | ".join(t[0] for t in tiles) + " |")
    r.w("|" + "|".join(["---"] * len(tiles)) + "|")
    r.w("| " + " | ".join("**{}**".format(t[1]) for t in tiles) + " |", "")

    _highlights(r, df, started, sessions, snap)
    _activity(r, df, started, sessions, axis)
    _games(r, started, snap)
    _versions(r, snap, started, axis)
    _geography(r, snap)
    _os(r, snap)
    _engagement(r, sessions, snap, started)
    _features(r, snap)
    _crashes(r, started, sessions, crashes)
    _coverage(r, started, snap, dev_installs, dev_events)

    r.w("---")
    r.w("*Generated by `tools/analytics_report.py`. Charts in `charts/`. "
        "Re-run after each monthly Aptabase export; the raw `.parquet` files stay out of the repo.*")
    return r.save()


def _highlights(r, df, started, sessions, snap):
    """A short, skimmable TL;DR of the standout numbers, derived from the data."""
    bullets = []
    installs = snap["install_id"].nunique()
    games = started["game"].nunique()
    countries = int(snap["country_name"].replace("", pd.NA).nunique())
    dau_peak = int(started.groupby("date")["install_id"].nunique().max()) if len(started) else 0
    bullets.append("**Reach:** {:,} installs across {} game{} in {} countries; "
                   "peak {:,} active on a single day.".format(
                       installs, games, "" if games == 1 else "s", countries, dau_peak))

    top_game = started["game"].value_counts()
    if len(top_game):
        bullets.append("**Main game:** {} - {} of launches.".format(
            top_game.index[0], pctstr(int(top_game.iloc[0]), len(started))))

    # platform (coverage-aware)
    osv = snap["os_version"].replace("", pd.NA).dropna()
    steam = snap["_n"].map(lambda n: n.get("steam_runtime"))
    steam = steam[steam.notna()]
    plat = []
    if len(osv):
        win11 = int(osv.astype(str).str.contains("Windows 11").sum())
        plat.append("{} run Windows 11".format(pctstr(win11, len(osv))))
    if len(steam):
        plat.append("{} use Steam".format(pctstr(int(steam.sum()), len(steam))))
    if plat:
        bullets.append("**Platform:** among installs reporting each field, "
                       + " and ".join(plat) + ".")

    lc = pd.to_numeric(snap["_n"].map(lambda n: n.get("launch_count")), errors="coerce").dropna()
    if len(lc):
        bullets.append("**Repeat use:** {} of installs launched more than once.".format(
            pctstr(int((lc > 1).sum()), len(lc))))

    primary = snap["game"].value_counts().index[0]
    huds = flag_adoption(snap[snap["game"] == primary], "hud_")
    if huds:
        k, en, rep = huds[0]
        bullets.append("**Most-used HUD:** {} - {} of {} installs.".format(
            _pretty_key(k), pctstr(en, rep), primary))

    # No stability headline here on purpose: the crash section below reports the same
    # figures with the exclusions and caveats attached, and a bare percentage at the top
    # of the report reads as a verdict without them.

    r.w("## Highlights", "")
    for b in bullets:
        r.w("- " + b)
    r.w("")


def partial_edge_days(df, min_coverage=0.9):
    """The first/last calendar days that the export only partly covers.

    An export is pulled at some instant, so its LAST day holds only the hours up to
    that instant -- 3,627 launches against 7,727 the day before, in the first report
    this was noticed on. Plotted as an ordinary point that is a cliff, and every
    line chart in the report ended on a downward hook that was an artifact of when
    somebody clicked export. The first day has the mirror problem whenever reporting
    began mid-day (the 1.26 rollout day held 2 events from 1 install).

    The summary ratio already guarded against this -- see the comment at the
    launches-per-install figure, which divides over the whole window precisely so a
    part-day cannot weigh as much as a full one -- but nothing applied the same
    reasoning to the daily series.

    Detected from the DATA rather than assumed: a boundary day is partial when the
    events on it do not span at least `min_coverage` of the day. A full day at this
    volume has events within minutes of both midnights, so the two cases are far
    apart; the threshold is not doing subtle work. Returns (drop_first, drop_last).
    """
    days = sorted(df["date"].unique())
    if len(days) < 3:            # nothing to trim to; leave the caller alone
        return False, False
    day_secs = 24 * 60 * 60

    def covered(day, from_start):
        ts = df.loc[df["date"] == day, "ts"]
        if ts.empty:
            return 0.0
        midnight = pd.Timestamp(day, tz="UTC")
        if from_start:           # first day: midnight -> first event
            return 1.0 - (ts.min() - midnight).total_seconds() / day_secs
        return (ts.max() - midnight).total_seconds() / day_secs

    return covered(days[0], True) < min_coverage, covered(days[-1], False) < min_coverage


def _duration(td):
    """A timedelta as the coarsest unit that still says something: '11 days', '15 h'."""
    hours = td.total_seconds() / 3600.0
    if hours >= 48:
        return "{:.0f} days".format(round(hours / 24))
    return "{:.0f} h".format(round(hours))


def collection_gaps(df, min_hours=3, dark_share=0.05):
    """Stretches inside the window where the export has no data because nothing was
    COLLECTING - not because nothing happened.

    Aptabase stops ingesting when the account's monthly event quota runs out, and the
    export simply has no rows for those hours. Nothing distinguishes that from a quiet
    night except the rate: a live hour in this window carries a median ~400 events and
    never fewer than ~35 outside an outage, so "almost nothing, for hours" is a
    different animal from "less than usual".

    Hence the test is RELATIVE, not a fixed floor: an hour is dark when it holds under
    `dark_share` of what the surrounding week's same-shape hours hold (a centred
    rolling median, so a trend or a weekend cannot move the bar much). Runs of dark
    hours shorter than `min_hours` are ignored - at low volume a genuinely empty hour
    happens - and a run touching either end of the window is left to
    partial_edge_days(), which is the same idea for the same reason at the edges.

    Returns [(start, end)] as UTC Timestamps, end EXCLUSIVE, oldest first. Detected
    from the data rather than remembered, like every other collection artifact here:
    nobody will annotate a gap by hand two exports later.
    """
    if df.empty:
        return []
    hourly = df.set_index("ts").resample("h").size()
    if len(hourly) < 24:
        return []
    # min_periods lets the edges have an expectation at all; centre so a gap does not
    # drag its own expectation down (a trailing window would learn the outage).
    expected = hourly.rolling(24 * 7, center=True, min_periods=24).median()
    dark = hourly < (dark_share * expected.clip(lower=1))
    runs, start = [], None
    for t, is_dark in dark.items():
        if is_dark and start is None:
            start = t
        elif not is_dark and start is not None:
            runs.append((start, t))
            start = None
    if start is not None:
        runs.append((start, hourly.index[-1] + pd.Timedelta(hours=1)))
    first_day, last_day = df["date"].min(), df["date"].max()
    out = []
    for a, b in runs:
        if (b - a) < pd.Timedelta(hours=min_hours):
            continue
        # An edge run is the export's own boundary, not an outage; see the docstring.
        if a.date() <= first_day or (b - pd.Timedelta(hours=1)).date() >= last_day:
            continue
        out.append((a, b))
    return out


def gap_days(gaps, day_share=0.1):
    """The calendar days a gap eats enough of to make their totals meaningless.

    A day that lost two hours still plots honestly; a day that lost twenty is a
    trough that never happened, and plotting it invites exactly the wrong reading
    ("usage collapsed on the 15th"). Same 90%-coverage line partial_edge_days uses,
    applied to the interior. Returns a set of dates.
    """
    out = set()
    for a, b in gaps:
        for d in pd.date_range(a.normalize(), b, freq="D"):
            day0 = d
            day1 = d + pd.Timedelta(days=1)
            lost = (min(b, day1) - max(a, day0)).total_seconds()
            if lost > 0 and lost / 86400.0 > day_share:
                out.add(day0.date())
    return out


def day_axis(df):
    """The day axis every daily chart in the report shares, and what is missing from it.

    Built ONCE and passed around: two sections deciding independently which days exist
    is how one ends up plotting a part-day the other dropped, or closing a gap the
    other shows.

    days      - every calendar day between the (trimmed) edges, INCLUDING days with no
                events. A day the exporter never saw used to be absent from the axis
                entirely, so the days either side of an eleven-day outage were drawn
                adjacent and the line walked straight across it: the chart said "quiet
                fortnight", the data said "nothing was listening".
    dark      - days a gap ate more than a tenth of. Their value in a series is None,
                which svg.lines() breaks the line at rather than drawing as zero.
    bands     - (i0, i1, label) index ranges for the shading.
    observed  - days that are actually plottable, i.e. the denominator for any average.
    """
    observed_days = sorted(df["date"].unique())
    drop_first, drop_last = partial_edge_days(df)
    lo = observed_days[1 if drop_first else 0]
    hi = observed_days[-2 if drop_last else -1]
    days = [d.date() for d in pd.date_range(lo, hi, freq="D")]
    gaps = collection_gaps(df)
    dark = gap_days(gaps)
    idx = {d: i for i, d in enumerate(days)}
    bands = []
    for a, b in gaps:
        ds = sorted(d for d in dark if d in idx
                    and a.date() <= d <= (b - pd.Timedelta(seconds=1)).date())
        if ds:
            bands.append((idx[ds[0]], idx[ds[-1]], "no data"))
    edges = [str(d) for d, drop in ((observed_days[0], drop_first),
                                    (observed_days[-1], drop_last)) if drop]
    return {"days": days, "labels": [d.strftime("%m-%d") for d in days], "dark": dark,
            "bands": bands, "observed": [d for d in days if d not in dark],
            "gaps": gaps, "edges": edges}


def _activity(r, df, started, sessions, axis):
    r.w("## Activity over time", "")
    days, dark_days, gap_bands = axis["days"], axis["dark"], axis["bands"]
    xlabels, plotted = axis["labels"], axis["observed"]
    if axis["edges"]:
        # Short on purpose: the reader needs to know the axis is trimmed, not the
        # reasoning. Why it is trimmed lives at partial_edge_days().
        r.w("> Daily charts omit partial export days ({}); totals include them."
            .format(", ".join(axis["edges"])), "")
    # NO NOTE ABOUT THE GAPS. The chart shades them and labels them "no data", which
    # is where a reader meets a hole in the first place, and the header already states
    # the coverage the averages divide by ("68 days, 51 observed"). A paragraph
    # restating both in prose was three sentences nobody needed; how a gap is detected
    # and why a day goes blank lives in analytics/README.md.

    def game_series(game_list):
        out = []
        for i, g in enumerate(game_list):
            by_day = started[started["game"] == g].groupby("date").size()
            # None, not 0, on a day nothing was collecting: zero launches is a claim
            # about players, and this is a claim about the exporter.
            out.append((g, [None if d in dark_days else int(by_day.get(d, 0)) for d in days],
                        svg.GAME_COLORS.get(g, svg.PALETTE[i % len(svg.PALETTE)])))
        return out

    # ONE chart, all games on a shared axis, not a major/minor split at a launch-share
    # threshold. A dominant game does flatten the rest to near-zero (MX Bikes is ~99%
    # of launches, so GP Bikes and Kart Racing Pro sit close to the axis here), but
    # that is not worth two charts plus a partition rule: the low-volume games are
    # legible in the Games table, and one chart cannot lose a game the way a
    # threshold can.
    totals = started.groupby("game").size().sort_values(ascending=False)
    games = list(totals.index)
    # LOG y axis whenever more than one game is plotted. MX Bikes runs ~5,000
    # launches/day against Kart Racing Pro's ~20: on a linear axis the small games are
    # pinned flat to the bottom and the chart only really shows one of them. A log
    # axis lets all three be read at once. Single-game data stays linear -- there is
    # no spread to compress,
    # and a log axis would just make an ordinary curve harder to read.
    if games:
        multi = len(games) > 1
        r.chart("activity_launches.svg",
                svg.lines("Daily launches" + (" by game" if multi else " - " + games[0]),
                          xlabels, game_series(games),
                          subtitle="launches per day, all games"
                                   + (" (log scale)" if multi else ""),
                          log=multi, gaps=gap_bands),
                "Daily launches by game" if multi else "Daily launches")

    # Cumulative distinct installs: the running total of installs seen at least once.
    # CAVEAT, and why the wording below is careful: an install enters this curve when it
    # FIRST REPORTS, not when the user installed the plugin. Analytics shipped in 1.26.0.0
    # (2026-06-28), so 99% of installs report install_age_days=0 at their first event and
    # nothing in the data predates that date -- the early ramp is existing users upgrading
    # onto an instrumented build, not new users. Calling it "user growth" would overstate
    # acquisition by most of the curve's height.
    # Split BY GAME, in the same colours the launches chart uses for them, so the two
    # charts in this section can be read against each other rather than as separate
    # worlds. An install belongs to exactly one game (the plugin installs per game), so
    # the per-game curves partition the total and the Total line is their sum -- not an
    # independent count that could disagree.
    # ONE GAME PER INSTALL, its most recent - the same rule the Games table and the
    # version chart use. It matters: 2 installs in this window report under two games
    # (the install id lives in the plugin's data folder, so a folder copied between
    # games carries its id along), and splitting the curves by each row's own game
    # counted those installs twice. Rare, and it broke the sum assertion below rather
    # than passing quietly, which is what the assertion is for.
    install_game = started.sort_values("ts").groupby("install_id")["game"].last()
    first_seen = started.groupby("install_id")["date"].min()

    def cumulative(ids):
        # The running total is carried THROUGH a gap (an install first seen after one
        # is still new to us) but not PLOTTED across it: drawing the curve there would
        # assert a flat stretch we cannot see, and the flat would read as "nobody
        # installed it that fortnight".
        first = first_seen.loc[ids].value_counts().sort_index() if len(ids) else {}
        run, out = 0, []
        for d in days:
            run += int(first.get(d, 0))
            out.append(None if d in dark_days else run)
        return out

    game_order = list(started.groupby("game").size().sort_values(ascending=False).index)
    cume_series = [(g, cumulative(install_game.index[install_game == g]),
                    svg.GAME_COLORS.get(g, svg.PALETTE[i % len(svg.PALETTE)]))
                   for i, g in enumerate(game_order)]
    cume_total = cumulative(install_game.index)
    # The total is the sum of the per-game curves by construction; assert it rather than
    # trust it, since a mismatch would mean an install counted under two games (or none)
    # and the chart would quietly disagree with the header tile.
    for di in range(len(days)):
        if cume_total[di] is None:
            continue
        assert sum(ser[1][di] for ser in cume_series) == cume_total[di], \
            "per-game cumulative installs do not sum to the total on {}".format(days[di])
    # NO Total line, deliberately: MX Bikes is 98.5% of installs, so on a log axis a
    # Total curve and the MX Bikes curve sit ~0.3px apart and whichever is drawn
    # second hides the other completely. A legend entry for a line nobody can
    # distinguish is worse than no line. The exact total is in the header tile, and the
    # assertion above still checks the per-game curves sum to it.
    # Log axis for the same reason as the launches chart: MX Bikes ends near 5,500
    # against Kart Racing Pro's 18, so a linear axis flattens two of the three games
    # onto the floor. Cumulative counts start at 0 on day one, which log10(1+v) places
    # on the floor honestly rather than dropping.
    r.chart("activity_cumulative_installs.svg",
            svg.lines("Cumulative installs seen", xlabels, cume_series,
                      subtitle="running total of distinct installs, by game (log scale)",
                      log=len(game_order) > 1, gaps=gap_bands),
            "Cumulative installs seen")
    # Just the total: "new in this window" would be the same number, since first-seen is
    # computed within the window itself. Counted over EVERY install rather than cume[-1],
    # which stops at the last full day -- an install first seen on a trimmed part-day
    # is still an install, and a headline total that quietly excluded it would be wrong in
    # a way nobody would catch.
    r.w("- **Installs seen to date:** {:,}".format(int(started["install_id"].nunique())), "")
    # avg over the SAME days the charts plot: `days` excludes partial edges, so the
    # numerator must not count launches on the part-days the denominator dropped, or
    # the figure rises ~6% for no real reason.
    by_day = started.groupby("date").size()
    full_day_launches = sum(int(by_day.get(d, 0)) for d in plotted)
    r.w("- **avg launches/day:** {:,.0f}".format(full_day_launches / max(1, len(plotted))), "")



def _games(r, started, snap):
    r.w("## Games", "")
    rows = []
    for g in sorted(set(started["game"])):
        g_installs = snap[snap["game"] == g]["install_id"].nunique()
        g_launch = int((started["game"] == g).sum())
        rows.append((g, g_installs, g_launch))
    rows.sort(key=lambda x: x[1], reverse=True)
    r.w("| Game | Installs | Launches | Share of launches |")
    r.w("|---|--:|--:|--:|")
    tot = sum(x[2] for x in rows) or 1
    for g, ins, la in rows:
        r.w("| {} | {:,} | {:,} | {:.1f}% |".format(g, ins, la, pct(la, tot)))
    r.w("")

MIN_VERSION_INSTALLS = 10  # versions below this are grouped as pre-release / dev builds
# A feature flag needs this many installs REPORTING it before it gets a bar; see the
# comment in the adoption renderer.
MIN_ADOPTION_BASE = 25
MIN_VERSION_SHARE = 0.01   # a version needs 1% of window launches to get its own line


def _versions(r, snap, started, axis):
    r.w("## Plugin version adoption", "")
    r.w("Each install counts once, at its **most recent** version (an upgrade moves it, "
        "never double-counts). Versions under {} installs are grouped.".format(
            MIN_VERSION_INSTALLS), "")
    vc = snap["app_version"].value_counts()
    total = len(snap)
    main = [(v, int(c)) for v, c in vc.items() if c >= MIN_VERSION_INSTALLS]
    tail = sum(int(c) for v, c in vc.items() if c < MIN_VERSION_INSTALLS)
    n_tail = sum(1 for v, c in vc.items() if c < MIN_VERSION_INSTALLS)
    # ONE colour per version, shared with the migration chart below. Letting each
    # renderer assign palette slots by ITS OWN ordering -- installs here, launches
    # there -- puts the same version green in one and blue in the other, which
    # reads as two different versions rather than one.
    vercolor = {v: svg.PALETTE[i % len(svg.PALETTE)] for i, (v, _c) in enumerate(main)}
    bars = [(v, c, vercolor[v], cp(c, total)) for v, c in main]
    if tail:
        bars.append(("Pre-release / dev ({} builds)".format(n_tail), tail, "#57606a",
                     cp(tail, total)))
    r.chart("versions.svg",
            svg.hbar("Installs by plugin version", bars,
                     subtitle="latest version seen per install", label_w=240),
            "Installs by plugin version")
    # MIGRATION OVER TIME. The bar chart above is a SNAPSHOT -- every install at its
    # latest version -- so it cannot show a rollout: an install that moved 1.26 -> 1.27
    # mid-window looks like it was always on 1.27. This plots each day's share of
    # launches by version, which is the shape a rollout actually has (one line falling
    # as another rises) and is what the snapshot silently flattens.
    #
    # SHARE, not counts: daily volume swings by a factor of ~3 across a week, and on
    # absolute axes every version rises and falls together with it, which reads as
    # everything changing at once when nothing has.
    #
    # Only versions that clear MIN_VERSION_SHARE of window launches get a line; this
    # window has 116 distinct versions, of which 4 carry >99% of launches and the rest
    # are single-digit builds that would be 112 lines of floor noise. The remainder is
    # summed into one "Other" line rather than dropped, so the lines still add to 100%.
    days, dark_days = axis["days"], axis["dark"]
    if len(days) > 1 and len(started):
        # Installs per day, not launches. Launch share over-weights heavy users -- a
        # player who starts the game ten times counts ten times -- and the question
        # "what is everyone running" is about people, not sessions. It also puts this
        # chart in the same unit as the bar chart above, so the two are comparable.
        #
        # Each install is credited to the last version it ran that day, the same rule
        # the bar chart uses ("an upgrade moves it, never double-counts"). Without
        # that, an install that upgrades mid-day appears under both versions and the
        # day sums past 100%.
        #
        # The populations still differ, and legitimately: the bar chart counts
        # every install seen in the whole window, this one only installs ACTIVE on a
        # given day. Measured here, 1.27.7.44 is 50% of all installs but 69% of the
        # ones active on the last day, because dormant installs on older versions
        # still sit in the bar chart's denominator. That is a real difference between
        # "who has it" and "who is playing", not a discrepancy to reconcile away.
        day_last = (started.sort_values("ts")
                           .groupby(["date", "install_id"])["app_version"].last()
                           .reset_index())
        per_day_total = day_last.groupby("date").size()
        vshare = day_last["app_version"].value_counts()
        named = [v for v in vshare.index
                 if vshare[v] >= MIN_VERSION_SHARE * len(day_last)]
        series = []
        for v in named:
            by_day = day_last[day_last["app_version"] == v].groupby("date").size()
            series.append((v, [None if d in dark_days else
                               100.0 * int(by_day.get(d, 0)) / max(1, int(per_day_total.get(d, 0)))
                               for d in days],
                           vercolor.get(v, svg.PALETTE[len(svg.PALETTE) - 1])))
        rest = day_last[~day_last["app_version"].isin(named)]
        if len(rest):
            by_day = rest.groupby("date").size()
            series.append(("Other ({} builds)".format(day_last["app_version"].nunique() - len(named)),
                           [None if d in dark_days else
                            100.0 * int(by_day.get(d, 0)) / max(1, int(per_day_total.get(d, 0)))
                            for d in days], "#57606a"))
        # Every day's lines must add to 100%: this is a share chart, and the remainder
        # series above is the only thing making that true. Drop it (or filter the named
        # set inconsistently) and the lines quietly stop summing -- which reads as
        # adoption that went nowhere rather than as a bug, so nothing downstream would
        # question it. Checked here rather than in the selftest because this is the
        # invariant holding for the REAL data, not for a fixture.
        for di in range(len(days)):
            if any(ser[1][di] is None for ser in series):
                continue
            total_share = sum(ser[1][di] for ser in series)
            assert abs(total_share - 100.0) < 0.5 or per_day_total.get(days[di], 0) == 0, \
                "version shares for {} sum to {:.1f}%, not 100 — a version is missing " \
                "from both the named set and the remainder".format(days[di], total_share)
        if series:
            r.chart("version_migration.svg",
                    svg.lines("Version migration", axis["labels"], series,
                              subtitle="share of each day's active installs, by plugin version",
                              value_fmt=lambda v: "{:.0f}%".format(v), gaps=axis["bands"]),
                    "Version migration over time")

    famc = snap["app_version"].map(ver_family).value_counts()
    r.w("**By release line:** " + "  ·  ".join(
        "`{}` {}".format(f, cp(c, total)) for f, c in famc.items()), "")
    ch = snap["_s"].map(lambda s: s.get("update_channel"))
    ch = ch[ch.notna()]
    if len(ch):
        cc = ch.value_counts()
        r.w("**Update channel:** " + "  ·  ".join(
            "{} {}".format(k, cp(v, len(ch))) for k, v in cc.items()), "")


def _geography(r, snap):
    r.w("## Geography", "")
    cn = snap["country_name"].replace("", pd.NA).dropna()
    top = cn.value_counts().head(15)
    if len(top):
        r.chart("geography.svg",
                svg.hbar("Installs by country (top 15)",
                         [(c, int(n), None, cp(n, len(cn))) for c, n in top.items()],
                         subtitle="{:,} of {:,} installs report a country".format(len(cn), len(snap))),
                "Installs by country")


def _os(r, snap):
    r.w("## Operating system", "")
    osv = snap["os_version"].replace("", pd.NA).dropna()
    if not len(osv):
        r.w("_No OS data reported by the installs in this window._", "")
        return

    def bucket(v):
        v = str(v)
        if "Proton" in v or "Wine" in v or "Linux" in v or "Darwin" in v:
            return "Linux / Proton / Wine"
        if "Windows 11" in v:
            return "Windows 11"
        if "Windows 10" in v:
            return "Windows 10"
        return "Other Windows"

    b = osv.map(bucket).value_counts()
    r.chart("os.svg",
            svg.hbar("Installs by OS",
                     [(k, int(v), None, cp(v, len(osv))) for k, v in b.items()],
                     subtitle="{:,} of {:,} installs report an OS".format(len(osv), len(snap))),
            "Installs by OS")

    # Steam vs standalone lives here rather than under Games: it describes the RUNTIME
    # an install is under, which is the same question this section asks, not which game
    # it plays. Coverage-aware on steam_runtime, like every other adoption figure.
    rep = snap[snap["_n"].map(lambda n: "steam_runtime" in n)]
    if len(rep):
        steam = int(rep["_n"].map(lambda n: bool(n.get("steam_runtime"))).sum())
        r.chart("runtime_steam.svg",
                svg.stacked_bar("Steam vs. standalone",
                                [("Steam", steam, svg.PALETTE[0]),
                                 ("Standalone", len(rep) - steam, svg.PALETTE[2])],
                                subtitle="{:,} installs reporting".format(len(rep))),
                "Steam vs standalone")


def _engagement(r, sessions, snap, started=None):
    r.w("## Repeat usage", "")
    lc = snap["_n"].map(lambda n: n.get("launch_count"))
    lc = pd.to_numeric(lc, errors="coerce").dropna()
    if len(lc):
        buckets = [("1", 1, 2), ("2–5", 2, 6), ("6–20", 6, 21),
                   ("21–100", 21, 101), ("100+", 101, 10**9)]
        cats = [(lab, int(((lc >= lo) & (lc < hi)).sum())) for lab, lo, hi in buckets]
        retained = int((lc > 1).sum())
        r.w("- **Returning installs (launched more than once):** {} of {:,} reporting  ·  "
            "**median launches per install:** {:.0f}".format(
                cp(retained, len(lc)), len(lc), lc.median()), "")
        r.chart("launch_counts.svg",
                svg.vbars("Lifetime launches per install", cats,
                          subtitle="{:,} installs reporting".format(len(lc))),
                "Launches per install")

    # How long a sitting actually lasts. duration_seconds rides every session_end, but
    # session_end ITSELF was added in 1.27 -- 1.26 sent 83k launches and zero of them, so
    # the naive total silently covered ~36% of launches and read as a full figure. Same
    # schema-evolution trap the crash section handles, so this reports the denominator the
    # same way: which versions can report at all, and what share of those launches did.
    # The reporting versions are DERIVED (a version with no session_end predates the
    # instrumentation), so this self-corrects as old versions age out.
    dur = pd.to_numeric(sessions["_n"].map(lambda n: n.get("duration_seconds")),
                        errors="coerce")
    dur = dur[dur.notna() & (dur > 0)] / 60.0
    if len(dur):
        sbuckets = [("<5 min", 0, 5), ("5-15", 5, 15), ("15-30", 15, 30),
                    ("30-60", 30, 60), ("1-2 h", 60, 120), ("2 h+", 120, 10**9)]
        scats = [(lab, int(((dur >= lo) & (dur < hi)).sum())) for lab, lo, hi in sbuckets]

        cover = ""
        if started is not None and len(started):
            # Keyed on the EXACT app_version, not the family: 1.26.0.0 (83k launches)
            # reports nothing, but private 1.26.3.x dev builds do, so a family-level rule
            # counts all of 1.26 as instrumented and the coverage figure collapses.
            reporting = set(sessions.loc[dur.index, "app_version"])
            n_inst = int(started["app_version"].isin(reporting).sum())
            n_old = len(started) - n_inst
            if n_inst:
                cover = ("  ·  from {} of launches on versions that report one"
                         .format(pctstr(len(dur), n_inst)))
            if n_old:
                cover += ("; {:,} launches on earlier versions report no session end"
                          .format(n_old))
        # Total is a FLOOR, not a total: every unreported session is missing from it.
        r.w("- **Median session:** {:.0f} min  ·  **{:,} sessions**{}".format(
            dur.median(), len(dur), cover), "")
        r.w("- **At least {:,.0f} days** of session time across those sessions".format(
            dur.sum() / 60.0 / 24.0), "")
        r.chart("session_length.svg",
                svg.vbars("Session length", scats,
                          subtitle="{:,} reported sessions; median {:.0f} min".format(
                              len(dur), dur.median())),
                "Session length distribution")


def _features(r, snap):
    # HUD/widget/feature availability is game-specific (e.g. ECU & Tyre Temp are
    # GP Bikes only, FMX/Records are MX Bikes only), and the plugin only emits a
    # flag where that HUD/widget exists. Pooling games would put a GP-Bikes-only
    # widget's rate next to MX-Bikes rates over wildly different bases, so adoption
    # is shown for the primary (most-installed) game - 98%+ of the base - where
    # every flag shares one denominator.
    gc = snap["game"].value_counts()
    primary = gc.index[0]
    psnap = snap[snap["game"] == primary]
    others = ["{} {:,}".format(g, int(c)) for g, c in gc.items() if g != primary]

    r.w("## Feature & HUD adoption - {}".format(primary), "")
    intro = ("HUDs and widgets differ by game, so adoption is shown for the primary game, "
             "{}, over its {:,} installs.".format(primary, len(psnap)))
    if others:
        intro += " *({} have too few installs for their own breakdown.)*".format(
            " and ".join(others))
    r.w(intro, "")

    def render(title, prefix, name, labeler):
        rows = flag_adoption(psnap, prefix)
        if not rows:
            return
        rep_max = max(rep for _, _, rep in rows) or 1
        # A FLOOR ON THE BASE, the same idea as MIN_VERSION_INSTALLS. A flag added in
        # a build almost nobody runs yet is reported by a handful of installs, and
        # "100% (1 of 1)" then sorts to the TOP of a chart ranked by share, above a
        # flag with ten thousand reports. The number
        # is not wrong, it is one install; charting it invites a reading it cannot
        # support. Named below the chart instead, with its base, so a new flag is
        # visible without pretending to be a rate.
        thin = [(k, en, rep) for k, en, rep in rows if rep < MIN_ADOPTION_BASE]
        rows = [row for row in rows if row[2] >= MIN_ADOPTION_BASE]
        if not rows:
            return

        def annot(en, rep):
            # Within one game a smaller base means a flag newer builds added; spell
            # it out so the % isn't read against the full install count.
            if rep and rep < 0.5 * rep_max:
                return "{} ({:,} of {:,})".format(pctstr(en, rep), en, rep)
            return pc(en, rep)

        bars = [(labeler(k), pct(en, rep) if rep else 0, None, annot(en, rep))
                for k, en, rep in rows]
        r.chart(name,
                svg.hbar(title, bars,
                         subtitle="% of {:,} {} installs".format(rep_max, primary),
                         value_fmt=lambda v: "{:.0f}%".format(v)),
                title)
        if thin:
            r.w("*Too few reports to rank (under {} installs): {}.*".format(
                MIN_ADOPTION_BASE,
                ", ".join("{} {:,} of {:,}".format(labeler(k), en, rep)
                          for k, en, rep in sorted(thin, key=lambda t: -t[2]))), "")

    render("Features (feat_*)", "feat_", "features.svg",
           lambda k: FEATURE_LABELS.get(k, _pretty_key(k)))
    render("HUD adoption (hud_*)", "hud_", "huds.svg", _pretty_key)
    render("Widget adoption (widget_*)", "widget_", "widgets.svg", _pretty_key)

    # Panel theme. Not a feat_ flag because it is a CHOICE among values rather
    # than on/off, and "none" is one of the answers -- running unthemed is a
    # preference worth seeing next to the themes, not an absence. The plugin
    # sends a label, never a user's own folder name (see core/analytics_theme.h),
    # so "custom" is as specific as a third-party theme ever gets here.
    th = psnap["_s"].map(lambda s: s.get("panel_theme"))
    th = th[th.notna()]
    if len(th):
        tc = th.value_counts()
        r.chart("panel_theme.svg",
                svg.hbar("Panel theme", [(str(k), int(v), None, cp(v, len(th)))
                                         for k, v in tc.items()],
                         subtitle="{:,} of {:,} {} installs report a theme "
                                  "(older builds do not)".format(len(th), len(psnap), primary)),
                "Panel theme")

    # counts of enabled HUDs/widgets per install (primary game)
    hc = pd.to_numeric(psnap["_n"].map(lambda n: n.get("hud_count")), errors="coerce").dropna()
    wc = pd.to_numeric(psnap["_n"].map(lambda n: n.get("widget_count")), errors="coerce").dropna()
    if len(hc):
        r.w("- **Median HUDs enabled per install:** {:.0f}  ·  "
            "**median widgets:** {:.0f}".format(hc.median(), wc.median() if len(wc) else 0), "")


def crash_population(started, crashes):
    """Shared crash filtering used by the crash section and the highlights.

    Returns (cr, stmin, meta) where cr is the reliable, non-dev-host crash frame
    (with fault/code/av/game_build/cpv columns), stmin is the matching 1.27.5+
    launch frame, and meta carries the excluded counts."""
    c = crashes.copy()
    c["host"] = c["_s"].map(lambda s: (s.get("host") or ""))
    c["fault"] = c["_s"].map(lambda s: s.get("fault") or "unknown+0x0")
    c["code"] = c["_s"].map(lambda s: s.get("code"))
    c["av"] = c["_s"].map(lambda s: s.get("av_type"))
    c["game_build"] = c["_s"].map(lambda s: s.get("game_build"))
    c["cpv"] = c["_s"].map(lambda s: s.get("crash_plugin_version"))
    # (1) drop dev/replay tooling hosts; (2) keep only crashes from plugin 1.27.5+,
    # where crash telemetry became fully instrumented (backtrace + av_type on top of
    # the version/build pinning). Rate is per launch, not per sampled session_end.
    dev = c["host"].str.lower().isin(DEV_HOSTS)
    reliable = c["cpv"].map(lambda v: ver_ge(v, *CRASH_MIN))
    cr = c[~dev & reliable].copy()
    stmin = started[started["app_version"].map(lambda v: ver_ge(v, *CRASH_MIN))]
    meta = {"dev_n": int(dev.sum()), "pre_n": int((~dev & ~reliable).sum())}
    return cr, stmin, meta


def _crashes(r, started, sessions, crashes):
    r.w("## Crashes (upstream / stability)", "")
    _, lookup = load_known_crashes()
    KGC = "../crash_analysis/KNOWN_GAME_CRASHES.md"

    cr, stmin, meta = crash_population(started, crashes)
    dev_n, pre_n = meta["dev_n"], meta["pre_n"]
    launches = len(stmin)
    affected = cr["install_id"].nunique()
    base_installs = stmin["install_id"].nunique()
    r.w("Of **{:,}** crash reports, **{:,}** come from plugin **{}+** builds, which record full "
        "diagnostics (a backtrace and the access-violation type). The rest are excluded from "
        "the analysis below: {:,} from earlier builds and {:,} from dev tooling.".format(
            len(crashes), len(cr), CRASH_MIN_STR, pre_n, dev_n), "")
    r.w("- **Crash-report rate:** {:.1f}% ({:,} of {:,} launches)".format(
        pct(len(cr), launches), len(cr), launches))
    r.w("- **Affected installs:** {:,} of {:,} running {}+ ({})".format(
        affected, base_installs, CRASH_MIN_STR, pctstr(affected, base_installs)), "")
    r.w("> The plugin's crash handler detects a fault, saves a report, and submits it on the "
        "next launch. Where a fault location was recorded, execution failed **outside the "
        "plugin binary** (in the game or another module), though that location is not "
        "necessarily the root cause. Reports are grouped by faulting module below and linked "
        "to the [known-crash list]({}), where a fix or workaround may exist.".format(KGC), "")

    # by game (1.27.5+)
    r.w("### Crash rate by game", "")
    r.w("| Game | Crash reports | {}+ launches | Launches with a crash report |".format(CRASH_MIN_STR))
    r.w("|---|--:|--:|--:|")
    small = False
    for g in sorted(set(cr["game"]) | set(stmin["game"])):
        gc = int((cr["game"] == g).sum())
        gl = int((stmin["game"] == g).sum())
        if gl or gc:
            rate = "{:.1f}%".format(pct(gc, gl))
            if gl < 100:  # too few launches for a stable rate
                rate += " *"
                small = True
            r.w("| {} | {:,} | {:,} | {} |".format(g, gc, gl, rate))
    if small:
        r.w("", "*\\* based on fewer than 100 launches; interpret cautiously.*")
    r.w("")

    # category breakdown
    cr["module"] = cr["fault"].map(lambda f: f.split("+")[0] if f else "unknown")
    cr["category"] = cr["module"].map(categorize_module)
    catc = cr["category"].value_counts()
    r.chart("crash_categories.svg",
            svg.hbar("Crashes by faulting-module category",
                     [(k, int(v), None, cp(v, len(cr))) for k, v in catc.items()],
                     subtitle="where the fault occurred", label_w=170),
            "Crashes by category")
    plug_n = int(catc.get("Plugin (MXBMRP3)", 0))
    if plug_n:
        r.w("*The Plugin (MXBMRP3) slice ({} crash{}) is where the fault offset landed, not "
            "proof the plugin caused it - inspect each via its backtrace.*".format(
                plug_n, "" if plug_n == 1 else "es"), "")

    # av type + exception code (one compact line)
    av = cr["av"].dropna()
    codec = cr["code"].dropna().value_counts().head(4)
    parts = []
    if len(av):
        parts.append("**Access-violation type:** " + ", ".join(
            "{} {:,}".format(k, int(v)) for k, v in av.value_counts().items()))
    if len(codec):
        parts.append("**exception codes:** " + ", ".join(
            "`{}` {:,}".format(k, int(v)) for k, v in codec.items()))
    if parts:
        r.w("  ·  ".join(parts), "")

    # Resolve each crash to a catalogued crash (or None), then present ONE ranked
    # table per unit: named crashes by name (with trigger + workaround), and the
    # uncatalogued tail by fault signature - so no crash is listed twice.
    cr["known"] = cr.apply(lambda row: match_known(row["fault"], row["game_build"], lookup), axis=1)
    matched = int(cr["known"].notna().sum())

    by_known = defaultdict(int)
    kmeta = {}
    for _, row in cr[cr["known"].notna()].iterrows():
        k = row["known"]
        by_known[k["id"]] += 1
        kmeta[k["id"]] = k

    r.w("### Most common crashes", "")
    r.w("Ranked by number of crash reports. **{}** of reports match signatures already "
        "catalogued in [`known_game_crashes.json`]({}); each links to its full write-up.".format(
            pc(matched, len(cr)), KGC), "")
    if by_known:
        r.w("| Crash | Share | Trigger | Fix / workaround |")
        r.w("|---|--:|---|:--:|")
        for cid, cnt in sorted(by_known.items(), key=lambda x: x[1], reverse=True):
            k = kmeta[cid]
            trig = (k.get("summary") or k.get("trigger") or k.get("when") or "").strip()
            if len(trig) > 100:
                trig = trig[:97].rstrip() + "…"
            wk = "[✔]({})".format(KGC) if k.get("workaround") else "-"
            r.w("| [{}]({}) | {} | {} | {} |".format(k["name"], KGC, pc(cnt, len(cr)), trig, wk))
        r.w("")
        r.w("*✔ = a documented workaround (follow the crash link). Matched on game build + "
            "fault offset.*", "")

    # Uncatalogued tail - by fault signature, for whoever extends the catalogue.
    unc = cr[cr["known"].isna()]
    if len(unc):
        sig = unc.groupby("fault").size().sort_values(ascending=False)
        r.w("", "### Not yet catalogued", "")
        r.w("The remaining **{}** of reports do not yet match the catalogue. The most frequent "
            "unmatched signatures are listed below by module and per-build offset:".format(
                pc(len(unc), len(cr))), "")
        r.w("| Fault (module + offset) | Category | Share |")
        r.w("|---|---|--:|")
        for fault, n in sig.head(10).items():
            r.w("| `{}` | {} | {} |".format(fault, categorize_module(fault.split("+")[0]), pc(n, len(cr))))
        r.w("")


def _coverage(r, started, snap, dev_installs=0, dev_events=0):
    r.w("## About this data", "")
    r.w("The plugin only sends anonymous, aggregate telemetry (no personal data - see the "
        "privacy note in the main README). Reporting began in **1.26** (a few stray older "
        "builds send only a basic launch event) and expanded through **1.27**, so older "
        "versions report fewer fields. Percentages are always taken over the installs that "
        "actually report a given field, and each chart notes how many installs that is, so "
        "incomplete rollout is never shown as a real trend.", "")
    r.w("**Definitions.** *Install* - a unique `install_id` (regenerated if the analytics "
        "file is deleted, so a wipe-and-reinstall reads as a new install). *Launch* - one "
        "plugin start (`app_started`). *Active install* - an install that launched on a given "
        "day. *Crash report* - one fault caught by the crash handler, saved on crash and sent "
        "on the next launch (≈ one per crashed launch). Each install belongs to one game (the "
        "plugin installs separately per game); its **game**, **country**, and **version** are "
        "its most recently seen values.", "")
    if dev_installs:
        r.w("Developer/test machines are excluded report-wide ({} install{}, {:,} events): "
            "they launch every dev build and deliberately trigger crashes to validate the "
            "telemetry, which would otherwise read as phantom plugin crashes.".format(
                dev_installs, "" if dev_installs == 1 else "s", dev_events), "")
    started = started.copy()
    started["fam"] = started["app_version"].map(ver_family)
    fields = {
        "Features": lambda row: any(k.startswith("feat_") for k in row["_n"]),
        "HUDs / widgets": lambda row: any(k.startswith(("hud_", "widget_")) for k in row["_n"]),
        "OS version": lambda row: bool(row["os_version"]),
        "Update channel": lambda row: "update_channel" in row["_s"],
        "Panel theme": lambda row: "panel_theme" in row["_s"],
        "Crash detail": lambda row: ver_ge(row["app_version"], *CRASH_MIN),
    }
    fams = sorted(started["fam"].unique())
    r.w("What each release line reports (share of its launches):", "")
    r.w("| Release line | Launches | " + " | ".join(fields) + " |")
    r.w("|---|--:|" + "|".join(["--:"] * len(fields)) + "|")
    for fam in fams:
        sub = started[started["fam"] == fam]
        if not len(sub):
            continue
        cells = ["{:.0f}%".format(100 * (sub.apply(fn, axis=1).mean() if len(sub) else 0))
                 for _, fn in fields.items()]
        r.w("| `{}` | {:,} | {} |".format(fam, len(sub), " | ".join(cells)))
    r.w("")


def selftest():
    """Exercise the whole pipeline on synthetic data (no export needed).

    Asserts the load-bearing invariants: install_id (not user_id) drives install
    counts, dev-host crashes are excluded, the known-crash join works, features
    are coverage-aware, and every chart/report file is produced."""
    import tempfile

    rows = []

    def ev(name, ts, uid, iid, sp=None, npr=None):
        s = {"install_id": iid, "game": "MX Bikes"}
        s.update(sp or {})
        rows.append({
            "timestamp": ts, "user_id": uid, "session_id": uid + "-s",
            "event_name": name, "string_props": json.dumps(s),
            "numeric_props": json.dumps(npr or {}),
            "os_name": "Windows", "os_version": (sp or {}).pop("_os", "Windows 11 (26200)"),
            "locale": "en-us", "app_version": (sp or {}).get("_ver", "1.27.5.43"),
            "app_build_number": "43", "engine_name": "", "engine_version": "",
            "country_code": "US", "country_name": "United States", "region_name": "CA",
        })

    base = 1_760_000_000
    # One real install, two DIFFERENT rotating user_ids on two days -> must count as 1 install.
    flags = {"feat_overlay": 1, "feat_rumble": 0, "hud_map": 1, "hud_standings": 0,
             "widget_speed": 1, "hud_count": 5, "widget_count": 3, "launch_count": 2,
             "steam_runtime": 1}
    # panel_theme rides on install-1 only, so the theme chart's denominator is the
    # REPORTING installs (1) rather than all of them (2) -- the same coverage-aware
    # shape the feat_* flags have, and the state the world is actually in while
    # older builds are still out there.
    ev("app_started", base, "userA1", "install-1", sp={"panel_theme": "carbon-dark"}, npr=flags)
    ev("app_started", base + 86400, "userA2", "install-1",
       sp={"panel_theme": "carbon-dark"}, npr=flags)
    ev("session_end", base + 100, "userA1", "install-1", npr={"duration_seconds": 600})
    # A minimal (early-build) launch with NO feature flags -> coverage must exclude it.
    ev("app_started", base + 200, "userB1", "install-2",
       sp={"_ver": "1.26.0.0", "_os": ""}, npr={"launch_count": 1})
    # A player crash (1.27+, reliable) that matches a catalogued known crash.
    ev("crash", base + 300, "userA1", "install-1",
       sp={"host": "mxbikes.exe", "fault": "mxbikes.exe+0x1f1923", "crash_plugin_version": "1.27.5.43",
           "game_build": "0x6A21833D", "code": "0xC0000005", "av_type": "read"})
    # A dev/replay-tool crash -> MUST be excluded from the player-facing rate.
    ev("crash", base + 400, "userA1", "install-1",
       sp={"host": "mxbmrp3_replay.exe", "fault": "mxbmrp3.dlo+0x1234", "crash_plugin_version": "1.27.5.43",
           "game_build": "0x6A21833D", "code": "0xC0000005"})
    # A 1.27.4 crash -> MUST be excluded: it predates the 1.27.5 full instrumentation
    # (no backtrace / av_type), which is the actual boundary being tested.
    ev("crash", base + 500, "userB1", "install-2",
       sp={"host": "mxbikes.exe", "fault": "mxbikes.exe+0x1f1923", "crash_plugin_version": "1.27.4.39",
           "game_build": "0x6A21833D", "code": "0xC0000005"})

    core_rows = [dict(x) for x in rows]  # snapshot for the direct-helper assertions

    # A developer/test install -> MUST be dropped report-wide (launch + test crash).
    dev_id = next(iter(DEV_INSTALL_IDS))
    ev("app_started", base + 600, "userD1", dev_id, npr=flags)
    ev("crash", base + 700, "userD1", dev_id,
       sp={"host": "mxbikes.exe", "fault": "mxbmrp3.dlo+0xdead", "crash_plugin_version": "1.27.5.43",
           "game_build": "0x6A21833D", "code": "0xC0000005", "av_type": "write"})

    def mkdf(row_list):
        d = pd.DataFrame(row_list)
        d["_s"] = [json.loads(v) for v in d["string_props"]]
        d["_n"] = [json.loads(v) for v in d["numeric_props"]]
        d["install_id"] = [s.get("install_id") for s in d["_s"]]
        d["game"] = [s.get("game") for s in d["_s"]]
        d["ts"] = pd.to_datetime(d["timestamp"], unit="s", utc=True)
        d["date"] = d["ts"].dt.date
        return d

    # Direct-helper invariants on the core fixture (no dev install).
    core = mkdf(core_rows)
    snap = latest_per_install(core[core.event_name == "app_started"])
    assert snap["install_id"].nunique() == 2, "install_id should collapse rotating user_ids"
    assert core["user_id"].nunique() == 3, "sanity: 3 rotating user_ids in fixture"

    fa = dict((k, (en, rep)) for k, en, rep in flag_adoption(snap, "hud_"))
    assert "hud_count" not in fa, "hud_count must not be treated as an adoption flag"
    assert fa["hud_map"] == (1, 1), "hud_map: 1 enabled of 1 reporting (install-2 excluded)"

    _, lookup = load_known_crashes()
    assert match_known("mxbikes.exe+0x1f1923", "0x6A21833D", lookup), "known-crash join failed"
    assert match_known("mxbikes.exe+0x1f1923", "0xDEADBEEF", lookup) is None, \
        "offset must not match a different build"
    # The build-hash join must be case-insensitive (plugin emits uppercase today).
    assert match_known("mxbikes.exe+0x1f1923", "0x6a21833d", lookup), \
        "known-crash join must be case-insensitive on the build hash"

    # pctstr caps: '100%' only for the literal whole; near-whole reads '>99%'.
    assert pctstr(1000, 1000) == "100%"
    assert pctstr(999, 1000) == ">99%"
    assert pctstr(3, 1000) == "<1%"

    # Full pipeline including the dev install, which build() must drop report-wide.
    out = tempfile.mkdtemp(prefix="analytics_selftest_")
    path = build(mkdf(rows), out)
    md = open(path).read()
    # 1 pre-threshold + 1 dev-host crash excluded, leaving exactly 1 counted player crash;
    # the dev install's launch + test crash must not appear.
    assert "1 from earlier builds and 1 from dev tooling" in md, "crash exclusions not reported"
    assert "**Affected installs:** 1 of" in md, "dev-install test crash not excluded"
    assert "Developer/test machines are excluded" in md, "dev-install exclusion not reported"
    assert "Plugin (MXBMRP3)" not in md, "no plugin-module crash should survive dev exclusion"
    assert "About this data" in md
    assert os.path.exists(os.path.join(out, "charts", "crash_categories.svg"))

    # The per-game activity chart must exist AND be referenced. Producing FEWER
    # charts is not an error unless something asserts otherwise, so a section that
    # silently drops its main game (this fixture is single-game, like the real data
    # at ~99% MX Bikes) would still print `selftest OK`.
    assert os.path.exists(os.path.join(out, "charts", "activity_launches.svg")), \
        "per-game activity chart missing — a game with data fell through both the " \
        "major and minor branches, which must PARTITION the games"
    assert "activity_launches.svg" in md, \
        "activity chart produced but REPORT.md does not reference it"

    # The panel-theme chart, both halves: produced AND referenced. A categorical
    # prop is rendered by its own branch rather than by the feat_ loop, so it is
    # not covered by any of the assertions above -- and a section that silently
    # renders nothing is precisely the failure this file exists to catch (see the
    # per-game chart assertion directly above).
    assert os.path.exists(os.path.join(out, "charts", "panel_theme.svg")), \
        "panel_theme chart missing — the theme label reached the payload but no " \
        "section renders it, so the data would be collected and never seen"
    assert "panel_theme.svg" in md, \
        "panel theme chart produced but REPORT.md does not reference it"

    # partial_edge_days keys off COVERAGE, not position: a boundary day is only
    # trimmed when the export genuinely stops short of it. Asserted both ways because
    # the failure modes are opposite and both silent -- never trimming leaves the
    # export-time cliff in every chart, always trimming discards a real day from a
    # window that happens to end at midnight.
    def _frame(last_hour):
        ts = ([pd.Timestamp("2026-01-01 00:05", tz="UTC"), pd.Timestamp("2026-01-01 23:55", tz="UTC")]
              + [pd.Timestamp("2026-01-02 00:05", tz="UTC"), pd.Timestamp("2026-01-02 23:55", tz="UTC")]
              + [pd.Timestamp("2026-01-03 00:05", tz="UTC"),
                 pd.Timestamp("2026-01-03 {:02d}:00".format(last_hour), tz="UTC")])
        f = pd.DataFrame({"ts": ts})
        f["date"] = f["ts"].dt.date
        return f

    assert partial_edge_days(_frame(23)) == (False, False), \
        "a window that runs to the end of its last day must not be trimmed"
    assert partial_edge_days(_frame(16)) == (False, True), \
        "an export cut off mid-day must trim that day from the daily series"
    # And a late-starting first day is caught by the same rule.
    late = _frame(23)
    late = late[~((late["ts"].dt.date == pd.Timestamp("2026-01-01").date())
                  & (late["ts"].dt.hour < 12))]
    assert partial_edge_days(late)[0] is True, \
        "a first day that only starts reporting mid-day must be trimmed too"

    # THE ADOPTION FLOOR. A flag two installs report is not a 100% adoption rate, and
    # ranking by share puts it at the TOP of the chart, above a flag with ten
    # thousand reports. Asserted on the same shape:
    # a well-reported flag ranks, a barely-reported one is held back whatever its share.
    wide = [{"feat_old": 1} for _ in range(MIN_ADOPTION_BASE)]
    thin_rows = [{"feat_old": 0, "feat_new": 1} for _ in range(2)]
    fsnap = pd.DataFrame({"_n": wide + thin_rows})
    got = {k: (en, rep) for k, en, rep in flag_adoption(fsnap, "feat_")}
    assert got["feat_new"] == (2, 2) and got["feat_old"][1] >= MIN_ADOPTION_BASE, \
        "flag_adoption must report each flag's own base, got {}".format(got)
    assert got["feat_new"][0] / got["feat_new"][1] > got["feat_old"][0] / got["feat_old"][1], \
        "the thin flag must out-RANK the wide one on share - that is what the floor is for"
    assert got["feat_new"][1] < MIN_ADOPTION_BASE <= got["feat_old"][1], \
        "the floor must separate these two, or the renderer's filter does nothing"

    # COLLECTION GAPS: the same reasoning as the edges, in the interior. Two failure
    # modes, both silent and both worse than a wrong number: miss the gap and the daily
    # line walks across eleven days that were never observed (which reads as a quiet
    # fortnight); call an ordinary quiet night a gap and the chart grows holes that are
    # really data. Both are asserted, on a frame with a known hole in it.
    def _busy(day_from, day_to, per_hour=20, hole=None):
        """A frame with per_hour events every hour, minus `hole` (a (start, end) pair)."""
        ts = []
        for d in pd.date_range(day_from, day_to, freq="h", tz="UTC"):
            if hole and hole[0] <= d < hole[1]:
                continue
            ts.extend([d + pd.Timedelta(minutes=m) for m in range(0, 60, 60 // per_hour)])
        f = pd.DataFrame({"ts": ts})
        f["date"] = f["ts"].dt.date
        return f

    hole = (pd.Timestamp("2026-02-10 00:00", tz="UTC"), pd.Timestamp("2026-02-14 00:00", tz="UTC"))
    gapped = _busy("2026-02-01", "2026-02-20 23:00", hole=hole)
    found = collection_gaps(gapped)
    assert len(found) == 1, "a four-day hole in a steady stream must be found once, got {}".format(found)
    assert found[0][0] == hole[0] and found[0][1] == hole[1], \
        "gap bounds must be the hole itself, got {}".format(found[0])
    assert gap_days(found) == {pd.Timestamp(d).date() for d in
                               pd.date_range("2026-02-10", "2026-02-13", freq="D")}, \
        "every day the gap covers must be marked unplottable"
    assert collection_gaps(_busy("2026-02-01", "2026-02-20 23:00")) == [], \
        "an uninterrupted stream must report no gaps"
    # A single quiet hour is not an outage: it is a night. min_hours is what separates
    # them, and without this case the detector would pepper the charts with holes.
    blip = (pd.Timestamp("2026-02-10 03:00", tz="UTC"), pd.Timestamp("2026-02-10 04:00", tz="UTC"))
    assert collection_gaps(_busy("2026-02-01", "2026-02-20 23:00", hole=blip)) == [], \
        "a one-hour lull must not be reported as a collection gap"
    # A day that loses only a couple of hours still plots: the threshold is a tenth.
    short = (pd.Timestamp("2026-02-10 01:00", tz="UTC"), pd.Timestamp("2026-02-10 03:00", tz="UTC"))
    assert gap_days([short]) == set(), "a 2-hour gap must not blank the whole day"
    # The axis carries the gap rather than closing it: 20 days in, 20 days out.
    ax = day_axis(gapped)
    assert len(ax["days"]) == 20, "the axis must keep every calendar day, got {}".format(len(ax["days"]))
    assert len(ax["observed"]) == 16 and ax["bands"], \
        "four of those days are unobserved and must be shaded, not dropped"

    # Log y axis over counts that REACH ZERO -- the one part of the log path that a
    # chart which "looks fine" cannot show you. log10(0) is undefined, so the renderer
    # scales log10(1+v); a zero has to land on the axis floor rather than raise, vanish,
    # or emit a non-finite coordinate that silently truncates the polyline. Kart Racing
    # Pro really does have zero-launch days, so this is the live case, not a hypothetical.
    log_svg = svg.lines("t", ["a", "b", "c", "d"],
                        [("s", [0, 1, 37, 7637], "#ffffff")], log=True)
    coords = re.findall(r'points="([^"]+)"', log_svg)
    assert coords, "log chart emitted no series"
    pts = [tuple(float(v) for v in pair.split(",")) for pair in coords[0].split()]
    assert len(pts) == 4, "a zero point was dropped from the log series"
    ys = [y for _x, y in pts]
    # ON CANVAS is the assertion that bites. Finiteness alone does not: substituting a
    # tiny epsilon for the zero (log10(1e-300)) is perfectly finite and lands the point
    # hundreds of heights off the chart, where it silently drags the polyline away.
    assert all(0 <= y <= 320 for y in ys), \
        "log axis put a point off-canvas — a zero count is being fed to log10() " \
        "with an epsilon instead of scaled as log10(1+v)"
    # And the zero belongs ON the floor: lowest on screen, i.e. the largest y.
    assert ys[0] == max(ys), "a zero count must sit on the axis floor"
    # Decade gridlines keep the labels in real units rather than log units.
    assert ">10,000<" in log_svg or ">10000<" in log_svg, "log axis lost its decade ticks"

    # Presence only. The share-sums-to-100 invariant is asserted inside the generator
    # against the REAL data instead -- a fixture check here would need a second copy
    # of the share arithmetic to compare against, and this assertion alone does not
    # catch a dropped remainder series.
    assert "version_migration.svg" in md or snap["app_version"].nunique() <= 1, \
        "multi-version data must produce a migration chart"

    # --- Export-format equivalence. CSV is the live path; both must land on
    # identical data. Written through the REAL
    # loader (read_export + to_utc), so a regression in either shows up here rather
    # than as silently-wrong dates in a published report.
    csv_dir = tempfile.mkdtemp(prefix="analytics_selftest_csv_")
    csv_path = os.path.join(csv_dir, "export.csv")
    # Aptabase's CSV renders timestamps as "YYYY-MM-DD HH:MM:SS", not epoch seconds.
    csv_rows = []
    for row in rows:
        r = dict(row)
        r["timestamp"] = (pd.Timestamp(row["timestamp"], unit="s", tz="UTC")
                          .strftime("%Y-%m-%d %H:%M:%S"))
        csv_rows.append(r)
    pd.DataFrame(csv_rows).to_csv(csv_path, index=False)
    cdf = load([csv_path])
    assert len(cdf) == len(rows), "CSV load dropped rows"
    assert cdf["install_id"].nunique() == mkdf(rows)["install_id"].nunique(), \
        "CSV load must yield the same installs as the in-memory fixture"
    assert pd.api.types.is_datetime64_any_dtype(cdf["ts"]), "CSV timestamps must parse"
    assert cdf["ts"].notna().all(), "CSV timestamp parse produced NaT"
    assert sorted(str(d) for d in cdf["date"].unique()) == \
        sorted(str(d) for d in mkdf(rows)["date"].unique()), "CSV dates must match parquet-style"
    # A missing optional field must read as "" (the sentinel the report treats as
    # absent), not NaN — that is what keep_default_na=False buys.
    assert (cdf["os_version"] == "").any(), "CSV blank must stay an empty string"
    # Aptabase's CSV concatenates paginated chunks and repeats the header row between
    # them. Left in, such a row parses as undated and only surfaces much later as a
    # TypeError inside a date reduction, so it is dropped at read time.
    with open(csv_path) as fh:
        head, body = fh.readline(), fh.read()
    hdr_path = os.path.join(csv_dir, "export_with_repeated_header.csv")
    with open(hdr_path, "w") as fh:
        fh.write(head + body + head)   # a stray header row mid-file
    hdf = load([hdr_path])
    assert len(hdf) == len(cdf), "repeated CSV header row must be dropped, not counted"
    assert hdf["ts"].notna().all(), "repeated header row leaked through as an undated event"
    # And the full pipeline runs on CSV-loaded data.
    csv_out = tempfile.mkdtemp(prefix="analytics_selftest_csvout_")
    assert os.path.exists(build(cdf, csv_out))

    print("selftest OK -> {}".format(path))


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser(
        description="Generate a static Markdown+SVG analytics dashboard from Aptabase exports (CSV or Parquet).")
    ap.add_argument("inputs", nargs="+", help="Export file(s) or globs (.csv or .parquet)")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "analytics"),
                    help="output directory (default: analytics/)")
    args = ap.parse_args()

    paths = []
    for pat in args.inputs:
        hit = sorted(glob.glob(pat))
        paths.extend(hit if hit else [pat])
    paths = [p for p in paths if os.path.exists(p)]
    if not paths:
        sys.exit("error: no input parquet files found")

    print("Reading {} file(s)...".format(len(paths)))
    df = load(paths)
    print("  {:,} events, {} → {}".format(len(df), df['date'].min(), df['date'].max()))
    out = build(df, args.out)
    print("Wrote {}".format(out))
    print("Charts in {}".format(os.path.join(args.out, "charts")))


if __name__ == "__main__":
    main()
