#!/usr/bin/env python3
# ============================================================================
# tools/analytics_report.py
# Turn Aptabase monthly exports into a static Markdown dashboard + SVG charts,
# checked into usage_survey/ (the raw exports are NOT kept in the repo).
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
from datetime import timedelta
from html import unescape as html_unescape

try:
    import pandas as pd
except ImportError:
    sys.exit("error: pandas is required (pip install -r tools/requirements.txt)")

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analytics_page as page  # noqa: E402
import analytics_rollup as rollup  # noqa: E402
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
    "d7983bfc-166e-457b-9be5-60e1d8c33c49",  # author - MX Bikes, until 2026-07-30 (id reset)
    "e44bd23d-4e50-40d0-9662-9398f7e9d4fe",  # author - GP Bikes
    "8b10ae0d-ec97-4807-b51b-4e6802d51fa5",  # author - MX Bikes since 2026-07-10, and Kart Racing Pro
}
# Every frame carries install ids ALREADY HASHED (see load()), so the exclusion has
# to match on the hash. Listing the raw ids above and deriving the set here keeps the
# comments above readable -- an opaque digest would say nothing about whose machine
# it is, and the rollup could not be re-derived from a list of digests.
DEV_INSTALL_HASHES = {rollup.iid(i) for i in DEV_INSTALL_IDS}

# Human labels for the stable feat_* flags (from analytics_manager.cpp). Unknown
# flags still render, prettified generically, so new features aren't dropped.
FEATURE_LABELS = {
    "feat_steam": "Steam friends",
    "feat_overlay": "Web overlay (OBS)",
    "feat_rumble": "Controller rumble",
    "feat_helmet": "Helmet overlay",
    "feat_director": "Auto-director",
    "feat_twitch": "Twitch chat (connected)",
    "feat_youtube": "YouTube chat (connected)",
    "feat_autoswitch": "Profile auto-switch",
    "feat_updates": "Update checker",
    "feat_widgets": "Widgets (master)",
    "feat_companion": "Companion window",
    "feat_thread": "Worker thread",
    # The two renderer flags are easy to confuse and are NOT the same window:
    # hwAccel is D3D11 in the companion window, glInGame is the plugin drawing the
    # IN-GAME HUD in the game's own GL context. Both report the setting, not the
    # backend that came up (see the SDK notes in analytics_manager.cpp).
    "feat_hwaccel": "GPU rendering (companion users)",   # base: FLAG_APPLIES_WHEN
    "feat_glingame": "Direct GL (in-game HUD)",
    # DERIVED here, not sent: the on/off half of the two label props (LABEL_PROPS).
    "feat_theme": "Panel theme (any)",
    "feat_spotter": "Spotter (audio callouts)",
    "feat_achievements": "Achievement toasts",
    "feat_devmode": "Developer mode",
    "feat_discord": "Discord presence",
}

# ----------------------------------------------------------------------------
# Loading & normalisation
# ----------------------------------------------------------------------------


_ACRONYMS = {"Fmx": "FMX", "Ecu": "ECU", "G Force": "G-Force", "Hud": "HUD", "Rpm": "RPM"}


# Rows of docs/achievements.md: | # | `id` | <img … title="icon"> | Title | Bronze |
# Silver | Gold | Platinum | Needs |. A tier cell is "-" where the achievement is a
# one-shot, and a description may carry a " (@credit)" suffix the report does not want.
# The leading cell is the row's position within its group, or "off" where the row
# is in kDisabledIds; it is consumed and discarded, so the capture groups below stay
# the columns that carry meaning. It is matched rather than skipped because a row
# that has LOST that column is a doc this regex should stop understanding, loudly.
# A switched-off row is kept, deliberately: people earned it before it was switched
# off, and dropping it here would label those rows by raw id with no icon or tiers.
_CAT_ROW = re.compile(
    r"\|\s*(?:\d+|off)\s*\| `([a-z0-9_]+)` \|(.*?)\| ([^|]+?) \| ([^|]*?) \| ([^|]*?) \| ([^|]*?) \| ([^|]*?) \|")
_CAT_ICON = re.compile(r'title="([a-z0-9-]+)"')
# The doc lays its rows out under one heading per Group, so the group a row belongs
# to is readable without a column for it -- which is the only way this tool can tell
# an UNLISTED row from an ordinary one. Renaming either group would silently start
# publishing them, so the selftest asserts the sections are found.
#
# TWO groups are unlisted, for two different reasons, and both belong here. Hidden
# is the spoiler case this filter was built for. Misfortune is unlisted in the
# plugin as well (Achievements::isUnlistedGroup), so its rows are equally a
# surprise to name -- and, more mechanically, the plugin leaves both out of the
# listed total, so a set that disagreed here would put this report's percentages
# out of step with the ones players see on their own tab.
HIDDEN_GROUPS = ("Hidden", "Misfortune")


def achievement_catalogue():
    """id -> {"title", "icon", "tiers", "group", "hidden"}, read off docs/achievements.md, which
    test_achievements.cpp GENERATES from the plugin's catalogue and diffs against the
    committed copy. Reading it (rather than a copy here) keeps the report's labels,
    icons and threshold wording on the same gate as the rows themselves, and is why
    none of it can drift from what the plugin actually ships. Missing file -> {} and
    the ids label themselves."""
    path = os.path.join(REPO_ROOT, "docs", "achievements.md")
    out = {}
    group = ""
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("## "):
                    # The heading carries a note for a group outside the completion
                    # figures ("Hidden (not counted)"); the GROUP is the name before
                    # it, which is what HIDDEN_GROUPS and every label here match on.
                    group = line[3:].split(" (")[0].strip()
                    continue
                m = _CAT_ROW.match(line)
                if not m:
                    continue
                icon = _CAT_ICON.search(m.group(2))
                tiers = []
                for cell in m.group(4), m.group(5), m.group(6), m.group(7):
                    cell = re.sub(r"\s*\(@[^)]*\)", "", cell).strip()
                    tiers.append(cell if cell not in ("", "-") else None)
                out[m.group(1)] = {"title": m.group(3).strip(),
                                   "icon": icon.group(1) if icon else None,
                                   "tiers": tiers,
                                   "group": group,
                                   "hidden": group in HIDDEN_GROUPS}
    except OSError:
        pass
    return out


_ICON_CACHE = {}


def icon_geometry(name):
    """(min_x, min_y, size, '<path data>') for an icon in assets/icons, or None.

    The path data is INLINED into the chart rather than referenced: a committed SVG
    that points at ../assets/icons/x.svg is rendered by GitHub as an image, and the
    reference is not fetched. Every shipped icon is a single-path Font Awesome glyph
    on one SQUARE viewBox, so this stays a regex rather than an XML dependency -- and
    returns None for anything that is not, so an icon that stops being one drops out of
    the chart instead of breaking it. The ORIGIN is returned rather than assumed: two
    of the shipped icons are drawn on "-64 -64 640 640", and a parser that only
    accepted "0 0" left exactly those two rows unillustrated."""
    if name in _ICON_CACHE:
        return _ICON_CACHE[name]
    got = None
    path = os.path.join(REPO_ROOT, "assets", "icons", (name or "") + ".svg")
    try:
        with open(path, encoding="utf-8") as f:
            svg = f.read()
        box = re.search(r'viewBox="(-?\d+) (-?\d+) (\d+) (\d+)"', svg)
        paths = re.findall(r'<path[^>]*\sd="([^"]+)"', svg)
        if box and box.group(3) == box.group(4) and len(paths) == 1:
            got = (int(box.group(1)), int(box.group(2)), int(box.group(3)), paths[0])
    except OSError:
        pass
    _ICON_CACHE[name] = got
    return got


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
    return derive(df)


def derive(df):
    """Add every column the report reads to a raw export frame.

    Shared with the selftest's fixture builder rather than mirrored there: the two
    drifted the moment a column was added here (a fixture with no `cov` column reaches
    the coverage table as a KeyError, and one with unhashed install ids walks straight
    past the developer exclusion), and a fixture that is not shaped like the real thing
    tests something else."""

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
    # HASHED HERE, ONCE, so the raw id cannot reach a committed artifact by any later
    # route: the rollup digest stores whatever this column holds. It is popped from the
    # props for the same reason -- an install's latest string_props IS persisted, and
    # the id would ride along inside it. Nothing downstream reads _s["install_id"].
    df["install_id"] = [rollup.iid(s.pop("install_id", None)) or None for s in df["_s"]]
    df["game"] = [s.get("game") or "Unknown" for s in df["_s"]]
    # Which fields each launch was ABLE to report, as a mask. The coverage table asks
    # this per launch, and a launch that is already in the rollup no longer carries the
    # props to answer it -- only this mask, which the digest stores. Computed for every
    # export the same way so the two paths cannot drift.
    df["cov"] = [rollup.coverage_bits(sp, np_, ov) if ev == "app_started" else 0
                 for ev, sp, np_, ov in zip(df["event_name"], df["_s"], df["_n"],
                                            df["os_version"])]
    # Marks a row that came from an export rather than the rollup; see build().
    df["hist"] = False
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
    return df.reset_index(drop=True)


def latest_per_install(started):
    """One row per install: its most recent app_started (current config snapshot).

    A TIE ON THE TIMESTAMP IS BROKEN BY launch_count, which the ping carries and which
    only ever goes up. Three installs in this data launched twice inside the same
    second; with the timestamp as the only key, `.last()` kept whichever of the pair the
    frame order happened to end on, so the same data snapshotted differently depending
    on how many exports were read in one run -- the last thing standing between a
    rolled-up month and the raw export reproducing each other exactly. A build that
    sends no launch_count sorts as -1, i.e. keeps the old frame-order behaviour among
    itself rather than jumping ahead of a build that does."""
    s = started[started["install_id"].notna()].copy()
    s["_lc"] = pd.to_numeric(s["_n"].map(lambda n: n.get("launch_count")),
                             errors="coerce").fillna(-1)
    s = s.sort_values(["timestamp", "_lc"], kind="stable")
    # drop_duplicates, not groupby().last(): the latter takes the last NON-NULL value
    # per COLUMN, so an install whose newest ping is missing a field would be handed
    # that field from an older row and the "snapshot" would be two pings spliced
    # together. Identical output whenever no column is null, which is why it never
    # showed up; taking whole rows means it cannot.
    return s.drop_duplicates("install_id", keep="last").drop(columns=["_lc"])


# ----------------------------------------------------------------------------
# Coverage-aware feature aggregation
# ----------------------------------------------------------------------------


# Numeric summary keys that share a flag prefix but are NOT 0/1 adoption flags.
_NOT_FLAGS = {"hud_count", "widget_count", "ach_pct", "ach_unlocked"}

# Flags that ARE 0/1 flags but say nothing as adoption: charted, each reads ~100%
# for a reason that is not anybody's choice. Named under their chart instead, with
# that reason, so a reader who misses the row finds out why.
NOT_CHARTED = {
    "widget_pointer": "always on",
    "widget_spotter": "follows the spotter, see Features",
}

# A setting that only does anything while another feature is on is counted over
# the installs running that feature, not everyone. GPU rendering defaults on and
# drives only the companion window, so over all installs it read 100% of people
# who never opened the window.
FLAG_APPLIES_WHEN = {
    "feat_hwaccel": "feat_companion",
}

# Choices the plugin reports as ONE label each: "none" when off, else a shipped
# pack's name, "custom" for a user's own (core/analytics_theme.h and
# core/analytics_spotter.h say why nothing else is sent). Each counts as a
# FEATURE: an on/off row in the features chart, DERIVED as `label != "none"`
# (no feat_ for it ever crosses the wire -- derive_label_flags). Which pack was
# chosen is still collected but no longer charted; a ranking of theme and
# voice-pack names said little the on/off row does not. Columns: prop, flag.
LABEL_PROPS = [
    ("panel_theme", "feat_theme"),
    ("spotter",     "feat_spotter"),
]


def derive_label_flags(snap):
    """Each LABEL_PROPS label as a feat_ flag: on unless the label is "none", so
    it ranks in the features chart like a sent flag. An install that sent no
    label (an older build) gets no key, so it stays out of that flag's base
    exactly as it would for a flag its build did not have. Returns a copy."""
    def derived(n, s):
        extra = {flag: (0 if s.get(prop) == "none" else 1)
                 for prop, flag in LABEL_PROPS if s.get(prop) is not None}
        return dict(n, **extra) if extra else n
    snap = snap.copy()
    snap["_n"] = [derived(n, s) for n, s in zip(snap["_n"], snap["_s"])]
    return snap


def flag_adoption(snap, prefix):
    """For every '<prefix>*' numeric flag, return
    [(key, enabled_installs, reporting_installs)], sorted by adoption %.
    Denominator is installs that actually SENT the flag (coverage-aware), and for
    a FLAG_APPLIES_WHEN setting only those running the feature it applies to."""
    enabled = Counter()
    reporting = Counter()
    for n in snap["_n"]:
        for k, v in n.items():
            if k.startswith(prefix) and k not in _NOT_FLAGS:
                cond = FLAG_APPLIES_WHEN.get(k)
                if cond and not n.get(cond):
                    continue   # the setting has no effect on this install
                reporting[k] += 1
                if v:
                    enabled[k] += 1
    rows = [(k, enabled[k], reporting[k]) for k in reporting]
    rows.sort(key=lambda r: (r[1] / r[2] if r[2] else 0, r[1]), reverse=True)
    return rows


def week_start(d):
    """The Monday of d's week: the cohort and rate axes use ISO weeks."""
    return d - timedelta(days=d.weekday())


def retention_curve(started, axis, max_days=30, since=None, min_size=None):
    """The retention curve: for N in 0..max_days, the share of new installs that
    launched again N or more days after they were first seen -- and the same curve
    for the newest weekly cohort old enough to be drawn, so the reader can see
    whether the latest arrivals stick better or worse than the pool.

    SURVIVAL, not "active on day N exactly": a player who rides on Sundays is retained
    at 7 days whichever Sunday it was, and the data is far too thin per day to ask
    the stricter question. Only installs that have had the FULL max_days to come
    back are counted (first seen max_days or more before the last observed day);
    a newer one is right-censored and would read as churn. `since` drops installs
    first seen before it -- the report passes the end of its first week, because
    that week is the existing user base arriving when analytics shipped, not new
    players, and it is stickier than anyone who installs later. Fewer than min_size
    installs draws nothing: a rate over five people is a coin toss.

    Returns (curve, installs, newest cohort Monday or None, its curve, its size).
    This replaced a chart with the cohort week on the x axis and one line per
    horizon: the lines stopped where each horizon ran out of data, at three
    different places, and the reader could not tell a cohort axis from a time
    axis. Days-since-install along the bottom is the shape everyone has seen."""
    if min_size is None:
        min_size = MIN_ADOPTION_BASE
    observed = axis["observed"]
    empty = ([], 0, None, [], 0)
    if not observed or not len(started):
        return empty
    per = started.groupby("install_id")["date"].agg(["min", "max"])
    lo = since if since is not None else observed[0]
    last_obs = observed[-1]
    measurable = per["min"].map(lambda d: d + timedelta(days=max_days) <= last_obs)
    per = per[(per["min"] >= lo) & measurable]
    if len(per) < min_size:
        return empty
    stayed = (per["max"] - per["min"]).map(lambda td: td.days)

    def curve(s):
        return [100.0 * int((s >= N).sum()) / len(s) for N in range(max_days + 1)]

    cohort = per["min"].map(week_start)
    newest, newest_curve, newest_n = None, [], 0
    for c in sorted(cohort.unique(), reverse=True):
        # The whole week must have had its max_days, not just the Monday's installs.
        if c + timedelta(days=6 + max_days) > last_obs:
            continue
        grp = stayed[cohort == c]
        if len(grp) < min_size:
            continue
        newest, newest_curve, newest_n = c, curve(grp), int(len(grp))
        break
    return curve(stayed), int(len(per)), newest, newest_curve, newest_n


GAME_STEMS = ("mxbikes", "gpbikes", "kart", "wrs")


def is_game_module(m):
    """True when a lowercased module name is the game executable. Matched on the
    stem, not the exact name: players rename the exe (an `mxbikesJ.exe` has
    reported crashes) and an exact-name list files those under third-party and
    never joins them to the catalogue, although the build hash still identifies
    the binary."""
    return m.endswith(".exe") and m.startswith(GAME_STEMS)


MODULE_CATEGORY = [
    # (predicate on lowercased module, category)
    (is_game_module, "Game"),
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


def known_category(crash):
    """Category for a catalogued crash. The catalogue knows the cause where the
    faulting module does not: the track-load crash faults inside msvcr90.dll, but
    it is the game handing the CRT a bad string, so by module alone it reads as
    'System / runtime' with the game one frame up the stack. An entry flagged
    `third_party` names who it belongs to instead."""
    tp = crash.get("third_party")
    return "Third-party ({})".format(tp) if tp else "Game"


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
    if is_game_module(m):
        return lookup.get(((game_build or "").lower(), off))
    return lookup.get((m, off))


# ----------------------------------------------------------------------------
# Report building
# ----------------------------------------------------------------------------


# Between two figures on ONE bullet. Wide on purpose: a bare "·" between two
# bold labels reads as punctuation inside a sentence rather than a break.
FIG_SEP = "  ·  "


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
        self.installs = None  # the lifetime total, for badge.json

    def w(self, *lines):
        self.md.extend(lines)

    def figures(self, *items):
        """The report's ONE shape for the numbers beside a chart: a tight bullet
        list, immediately ABOVE the chart or table it summarises.

        The page had three shapes for this one relationship and no rule saying
        which -- figures above the chart (Retention, Achievements), figures
        below it (Activity), and figures as bold prose that is a bullet
        everywhere else (the release lines, the access-violation split). Read
        together they look like three different kinds of statement rather than
        the same kind laid out three ways.

        ABOVE, because a figure read first frames the chart; read after, it is a
        footnote to a picture the reader has already had to interpret unaided.
        It also keeps a summary from stranding itself under the WRONG chart,
        which is how avg launches/day came to sit two charts below its own.

        TIGHT, because a blank line between items makes kramdown a LOOSE list --
        every item wrapped in <p>, the gaps roughly doubled - and the report
        rendered both shapes on the same page. Related figures belong in ONE
        item joined by FIG_SEP rather than as separate items.

        The one thing that still follows a chart is an italic CAPTION, which
        annotates the picture rather than summarising it.
        """
        items = [i for i in items if i]
        if items:
            self.md.extend(items)
            self.md.append("")

    def chart(self, name, svg_text, alt, html=False):
        """html=True writes an <img> tag instead of Markdown image syntax: inside a
        raw-HTML block such as <details>, Markdown syntax renders on GitHub but not
        in every engine (python-markdown left it as literal text), and a tag
        renders in all of them."""
        assert name not in self._chart_names, "duplicate chart " + name
        self._chart_names.add(name)
        # ONE title per chart. The SVG carries it (as its <title> and drawn
        # heading), and the alt text repeats it verbatim rather than a second
        # wording: a renderer that shows alt as a caption printed nineteen
        # captions that disagreed with the picture under them in five places.
        m = re.search(r"<title>(.*?)</title>", svg_text)
        if m:
            alt = html_unescape(m.group(1))
        with open(os.path.join(self.charts, name), "w") as f:
            f.write(svg_text)
        if html:
            self.md.append('<img src="charts/{}" alt="{}">'.format(name, alt))
        else:
            self.md.append("![{}](charts/{})".format(alt, name))
        self.md.append("")

    def save(self):
        """REPORT.md for the repo view, and index.html -- the same content with
        the charts inlined and interactive -- for GitHub Pages. One source, so
        the two can never report different numbers."""
        path = os.path.join(self.out, "REPORT.md")
        text = "\n".join(self.md).rstrip() + "\n"
        with open(path, "w") as f:
            f.write(text)
        page.write(self.out, text)
        if self.installs is not None:
            write_badge(self.out, self.installs)
        return path


def compact_count(n):
    """'982', '14.8k', '148k', '1.5M' - the badge's short form of a count, in the
    shape shields.io gives its own counts (the downloads badge read '82k')."""
    for limit, suffix in ((1000000, "M"), (1000, "k")):
        if n >= limit:
            v = n / limit
            text = "{:.1f}".format(v) if v < 100 else "{:.0f}".format(v)
            return (text[:-2] if text.endswith(".0") else text) + suffix
    return str(n)


def load_downloads(out_dir):
    """usage_survey/downloads.json (tools/release_downloads.py), or None when the
    file is absent - a render from the rollup alone, or a checkout predating the
    file, simply has no downloads figures rather than failing.

    Returns {"lines": {release line: (downloads, first release date)},
    "total": downloads, "releases": count, "since": first release date}."""
    path = os.path.join(out_dir, "downloads.json")
    if not os.path.exists(path):
        return None
    rels = json.load(open(path, encoding="utf-8"))["releases"]
    if not rels:
        return None
    lines = {}
    for rel in rels:
        fam = ver_family(rel["tag"].lstrip("v"))
        n, first = lines.get(fam, (0, None))
        dates = [d for d in (first, rel.get("published")) if d]
        lines[fam] = (n + int(rel["downloads"]), min(dates) if dates else None)
    dates = [rel["published"] for rel in rels if rel.get("published")]
    return {"lines": lines, "total": sum(int(rel["downloads"]) for rel in rels),
            "releases": len(rels), "since": min(dates) if dates else None}


def write_badge(out_dir, installs):
    """badge.json: the README's installs badge, in shields.io's endpoint format.

    WHY A FILE HERE AND NOT AN EDIT TO THE README. The README's badge URL is
    fixed and points at this file, so the number follows the report without this
    job ever writing to a hand-edited file it could conflict with. It reaches the
    public repo with the rest of usage_survey/ when the mirror is pushed, so the
    badge always agrees with the report it links to. Same figure as the
    Installs tile; written with a trailing newline and sorted keys, so a day with
    no new installs leaves no diff."""
    badge = {"schemaVersion": 1, "label": "unique installs",
             "message": compact_count(installs), "color": "blue"}
    with open(os.path.join(out_dir, "badge.json"), "w") as f:
        f.write(json.dumps(badge, sort_keys=True) + "\n")


def pct(a, b):
    return (100.0 * a / b) if b else 0.0


def adoption_annot(enabled, reporting):
    """An adoption bar's label: share, then count OF its own base, on EVERY row.
    Each flag's % is of the installs that sent it, and a flag newer builds added
    has a smaller base; the base used to be spelled out only below half the
    largest one, which read as a random mix of "98% (11,037)" and
    "100% (2,868 of 2,868)"."""
    return "{} ({:,} of {:,})".format(pctstr(enabled, reporting), enabled, reporting)


# One definition, in the chart module, so a hover readout and the label beside
# it cannot drift into two shapes for the same pair.
pctstr = svg.pctstr
pc = svg.pc


def ranked(series):
    """value_counts() with TIES BROKEN BY LABEL, most frequent first.

    Plain value_counts() leaves equal counts in whatever order the rows happened to
    arrive, so a chart's bars and a `.head(n)` cut-off both depend on the order the
    exports were read in. Two runs over the same data then disagree -- which is how the
    rollup round-trip first showed up as a "difference": two exception codes with 2
    hits each, and head(4) kept a different one each way."""
    return series.value_counts().sort_index(kind="stable").sort_values(
        ascending=False, kind="stable")


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

# Achievements ride the launch ping, but 1.30.0 ASSEMBLED that ping before StatsManager
# had loaded the save file, so every 1.30.0 install reports ach_pct=0 / ach_unlocked=0
# and no earned rows at all. That is a FALSE ZERO, and it is worse than the silence of a
# version predating the feature: silence is excluded by the "installs that report the
# field" denominator, while a zero is counted as a player who has earned nothing. 1.30.1
# moved analytics after the stats load, so the figures below are computed over 1.30.1+.
# Same shape as CRASH_MIN, for the same reason: one consistent population per metric.
ACH_MIN = (1, 30, 1)
ACH_MIN_STR = ".".join(map(str, ACH_MIN))


def build(df, out_dir, snap_hist=None):
    """Write the report. Returns (path, snapshot).

    The snapshot is returned because it is the one thing the rollup cannot re-derive
    from its own event digest: an install's LATEST payload. It is returned BEFORE the
    developer exclusion so the digest stays a faithful record of what was seen and the
    exclusion stays this function's decision, re-applied on every future run.
    """
    r = Report(out_dir)
    # One row per install. Rolled-up launches carry no props (only the latest per
    # install was kept), so they must not be candidates for "this install's current
    # config" -- the stored snapshot is that, and merge_installs() picks whichever of
    # the two is more recent per install.
    started_all = df[df.event_name == "app_started"]
    snap_all = rollup.merge_installs(
        snap_hist, latest_per_install(started_all[~started_all["hist"]]))

    # Drop developer/test installs report-wide before deriving anything.
    is_dev = df["install_id"].isin(DEV_INSTALL_HASHES)
    dev_installs = int(df.loc[is_dev, "install_id"].nunique())
    dev_events = int(is_dev.sum())
    df = df[~is_dev].copy()

    # UNIDENTIFIED LAUNCHES, dropped the same way and for a sharper reason: they
    # are not installs at all. AnalyticsManager counts the CURRENT launch
    # (`m_launchCount = launches + 1`), so a genuine first run reports 1 and the
    # only path that can report 0 is the `existingUnreadable` branch -- the
    # analytics file is there but cannot be opened or parsed, so the plugin mints
    # a SESSION-ONLY uuid, deliberately leaving the real file alone in case the
    # failure was transient.
    #
    # That id is never seen again, which is exactly what the data shows: every
    # one of them carries launch_count 0, install_age_days 0, version_status
    # "new", and precisely one app_started event. Counted as installs they were
    # 20.7% of the total -- inflating every install figure on the page by a fifth
    # and manufacturing most of an apparent "40% never came back", which is what
    # sent two retention charts to the bin above.
    #
    # PRESENT-AND-ZERO, never missing: launch_count arrived in telemetry schema 2.2.0 (the payload's
    # own version, not the plugin's -- see the coverage table), and
    # a build too old to send it is a real install whose count we simply do not
    # know. Testing `== 0` on a missing field would delete them.
    #
    # Their LAUNCH was real, and dropping it costs the launch totals ~0.6%. That
    # is the price of one coherent rule: an id we cannot trust as an identity is
    # not half-counted.
    lc_all = pd.to_numeric(snap_all["_n"].map(lambda n: n.get("launch_count")),
                           errors="coerce")
    phantom_ids = set(snap_all.loc[lc_all == 0, "install_id"])
    is_phantom = df["install_id"].isin(phantom_ids)
    phantom_installs = int(df.loc[is_phantom, "install_id"].nunique())
    phantom_events = int(is_phantom.sum())
    # A floor on how many MACHINES those ids are: the throwaway ids carry no
    # identity, but each ping still carries a country, an OS build and a plugin
    # version, and two pings that differ in any of the three are two machines.
    phantom_machines = int(snap_all.loc[snap_all["install_id"].isin(phantom_ids),
                                        ["country_name", "os_version", "app_version"]]
                           .drop_duplicates().shape[0])
    df = df[~is_phantom].copy()

    started = df[df.event_name == "app_started"].copy()
    sessions = df[df.event_name == "session_end"].copy()
    crashes = df[df.event_name == "crash"].copy()
    snap = snap_all[~snap_all["install_id"].isin(DEV_INSTALL_HASHES)
                    & ~snap_all["install_id"].isin(phantom_ids)]

    d0, d1 = df["date"].min(), df["date"].max()
    n_days = (d1 - d0).days + 1
    installs = snap["install_id"].nunique()

    # ---- Header + summary tiles ------------------------------------------
    r.w("# MXBMRP3 - Usage Survey Report", "")
    # The window states its COVERAGE, not just its ends. A reader comparing two
    # reports needs to know the denominator changed when a quota outage ate eleven
    # days of one of them; the gaps themselves are named in Activity over time.
    axis = day_axis(df)
    window = "**Data window:** {} → {} ({} days".format(d0, d1, n_days)
    window += ", {} observed)".format(len(axis["observed"])) if axis["gaps"] else ")"
    r.w(window, "")
    # The generated-doc note, in the repo's one shape: visible, after the intro.
    # It used to be an HTML comment up by the title AND an italic footer at the
    # end - invisible where it was read, and said twice.
    r.w("<sub>Generated by `tools/analytics_report.py` from Aptabase exports - do not edit."
        "</sub>", "")
    # Reach only. Deliberately NOT games (always ~3, and the Games section breaks it
    # down anyway) and NOT a raw crash-report count -- that number is meaningless
    # without the denominator and the 1.27.5+ instrumentation caveat, both of which
    # the Crashes section carries. A scary total up top invites the wrong reading.
    # Installs is a LIFETIME total and reads as an audience, so Active/day sits
    # next to it: 14,749 have been seen, ~1,900 are here on a given day. Without
    # it the top of the report has no figure for the plugin's current size.
    r.installs = installs
    downloads = load_downloads(out_dir)
    # "UNIQUE installs", and defined right under the tiles: beside Downloads, a
    # bare "Installs" reads as how often the installer was run.
    tiles = [
        ("Unique installs", "{:,}".format(installs)),
    ]
    # Downloads beside Installs, as the two measures of reach: every download of
    # the plugin from GitHub (the in-plugin updater included) against every
    # install that ever reported. Releases says how long the project has been
    # shipping, the one thing the survey window (1.26 on) cannot show.
    if downloads:
        tiles.append(("Downloads", "{:,}".format(downloads["total"])))
        since = ""
        if downloads["since"]:
            y, m = downloads["since"][:7].split("-")
            since = " since {} {}".format(
                ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
                 "Nov", "Dec")[int(m) - 1], y)
        tiles.append(("Releases" + since, "{}".format(downloads["releases"])))
    tiles += [
        ("Avg active/day", "{:,.0f}".format(avg_active_per_day(started, axis))),
        ("Launches", "{:,}".format(len(started))),
        ("Countries", str(snap["country_name"].replace("", pd.NA).nunique())),
    ]
    # ONE ROW PER FIGURE: six side by side outgrew a phone's width.
    r.w("| At a glance | |")
    r.w("|---|--:|")
    for name, value in tiles:
        r.w("| {} | **{}** |".format(name, value))
    r.w("")
    r.w("*Unique installs: plugin copies that have reported since 1.26, each counted once "
        "however often it is updated.{}*".format(
            " Downloads: installer and zip from GitHub, updates included." if downloads else ""),
        "")

    # SECTION ORDER: WHAT PLAYERS CARE ABOUT FIRST, what the developer cares
    # about after (the author's call, 2026-10-01). Players: who is here (Games),
    # what they do with it (Activity, Retention), where they are (Geography),
    # how they are doing (Achievements) and what they use (features). Then the
    # developer's half: OS, versions and downloads, and what breaks (Crashes).
    # Method last, where a reader goes to check rather than to learn.
    #
    # GEOGRAPHY STAYS AFTER THE USAGE SECTIONS - a deliberate call, not an
    # oversight. Moving it up beside Games ("who is here, all in one place")
    # was tried and rejected: the page should get to what the population DOES
    # within a screen of the top. Don't regroup it.
    #
    # NO HIGHLIGHTS SECTION. It lifted the top row out of three tables that sat
    # directly below it (main game, Windows 11, most-used HUD), once with a
    # different rounding, and the one bullet that was not a repeat -- retention
    # -- now has a section of its own.
    #
    # The two that used to sit high and no longer do: Activity opened the report
    # before anything said what the population WAS, and version adoption was
    # fourth -- ahead of every question about usage, on the strength of being
    # interesting to the developer rather than to the reader.
    _games(r, started, snap)
    _activity(r, df, started, sessions, axis)
    _retention(r, started, axis, snap)
    _geography(r, snap)
    _achievements(r, snap)
    _features(r, snap, started)
    _os(r, snap)
    _versions(r, snap, started, axis, downloads)
    _crashes(r, started, sessions, crashes)
    _coverage(r, started, snap, dev_installs, dev_events,
              phantom_installs, phantom_events, phantom_machines)

    # No trailing "generated by" line: the <sub> note under the data window says
    # it once, where a reader starting at the top actually meets it.
    return r.save(), snap_all


def _retention(r, started, axis, snap=None):
    """Do they come back, and how deep do the ones who stay go.

    The two figures lead as TEXT, not as a survival curve. The curve was drawn
    here for a while and it is the shape of the question rather than of the
    answer: it starts at 100% by definition and every later point is "still
    here", so it can only fall - and a line falling from top-left to
    bottom-right reads as decline to anyone glancing at the page, when 49% at
    day 30 is the strongest number in the report. The one thing the shape
    added (WHERE the loss happens: a cliff at day 1, then a gentle drift) is a
    sentence, and is stated as one. Under it, lifetime launches per install,
    which peaks in the middle, so the section ends on depth rather than decline.
    One pool over the whole population: per-month cohorts would answer "is it
    improving", but with a 93-day window whose first weeks are the existing
    user base upgrading, that is two honest cohorts drawn as a trend. Revisit
    at six months of genuine new installs."""
    r.w("## Retention", "")
    curve, pooled_n = [], 0
    if started is not None and len(started) and axis["observed"]:
        # The report's first week is left out: the existing user base arriving
        # when analytics shipped, not new players, and stickier than anyone who
        # installs later.
        since = axis["observed"][0] + timedelta(days=7)
        curve, pooled_n, _newest, _nc, _nn = retention_curve(started, axis, since=since)
    if curve:
        # "Playing with the plugin", not "launching": a ping is a GAME launch with
        # the plugin loaded, so an install that stopped playing the game looks
        # exactly like one that removed the plugin. The figure is therefore a
        # floor on the plugin's own retention, and the note says so - that one
        # line is what keeps a number that reads like a verdict on the plugin
        # honest in both directions.
        r.figures(
            "- **{}** of new installs are still playing with the plugin a month later: "
            "{:.0f}% after a week, {:.0f}% after a month, of {:,} installs old enough to "
            "measure.".format(_share_words(curve[30]), curve[7], curve[30], pooled_n),
            "- **{:.0f}% come back after their first day;** after the first week the curve "
            "is nearly flat.".format(curve[1]))
        r.w("> A floor: an install that stops playing looks the same as one that removed the "
            "plugin. The report's first week (existing users arriving) is not counted.", "")
    else:
        r.w("*Too few new installs a month old or more to measure retention yet.*", "")

    # Lifetime launches per install: how deep the ones who stay go.
    lc = pd.Series(dtype=float)
    if snap is not None and len(snap):
        lc = pd.to_numeric(snap["_n"].map(lambda n: n.get("launch_count")),
                           errors="coerce").dropna()
    if len(lc):
        # 100+ split at 500: it had grown to one in six installs, and the long
        # tail inside it (a few dozen past 500, a handful past 1,000) was invisible.
        buckets = [("1", 1, 2), ("2–5", 2, 6), ("6–20", 6, 21),
                   ("21–100", 21, 101), ("101–500", 101, 501), ("500+", 501, 10**9)]
        cats = [(lab, int(((lc >= lo) & (lc < hi)).sum())) for lab, lo, hi in buckets]
        r.chart("launch_counts.svg",
                svg.vbars("Lifetime launches per install", cats,
                          subtitle="installs reporting a launch count; median {:.0f}".format(
                              lc.median())),
                "Launches per install")


def _share_words(pct):
    """A share as the word a reader would use for it, for a headline that leads
    with the fact rather than the figure; the figure follows in the same line."""
    if pct >= 85:
        return "Most"
    if pct >= 70:
        return "Three quarters"
    if pct >= 62:
        return "Two thirds"
    if pct >= 45:
        return "Half"
    if pct >= 30:
        return "A third"
    if pct >= 20:
        return "A quarter"
    return "{:.0f}%".format(pct)


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


def avg_active_per_day(started, axis):
    """Distinct installs launching on an average day -- the report's size figure.

    ONE definition because three places print it (the header tile, the Reach
    highlight, and the Activity bullet beside the chart) and the denominator is
    the easy half to get wrong: it divides by the days the charts actually PLOT,
    so the partial export day the axis trims is out of both halves. Computed
    twice, that trimming is exactly what drifts -- the avg-launches bullet
    carries the same warning for the same reason.
    """
    plotted = axis["observed"]
    if not len(started) or not plotted:
        return 0.0
    by_day = started.groupby("date")["install_id"].nunique()
    return sum(int(by_day.get(d, 0)) for d in plotted) / len(plotted)


def breakdown(r, label, unit, pairs, total, note=None):
    """A breakdown -- category, count, share -- as a TABLE.

    THE PAGE'S CONTAINER RULE, of which this is the middle row:
        a distribution whose SHAPE is the point (time, buckets, many ranked
            categories)                                     -> chart
        any other breakdown                                 -> this
        a single scalar                                     -> Report.figures
        a number that needs its sentence to mean anything   -> prose

    The middle row is what the report had no rule for: the same handful of
    categories with one number each was a chart in three places, a bullet in two
    and prose in one, and the collisions were exact -- a two-way split was a
    stacked bar under Operating system and a half-line bullet under version
    adoption; a three-way split was an hbar there and a run-on sentence under
    Crashes.

    A TABLE rather than either. A chart of two or three categories is one long
    bar and a sliver you cannot read, and its axis implies a comparison the
    magnitudes do not repay; a bullet runs the labels and the numbers together
    into a sentence. A table is the shape the data already has, puts the numbers
    in a right-aligned column where they compare, and extends the ONE container
    rule the page was keeping everywhere (multi-column -> table) rather than
    inventing another.

    It costs height -- table rows carry more padding than bar rows, and the page
    grew ~700px adopting this. That is the trade: precision and one rule, not a
    shorter page.
    """
    r.w("| {} | {} |".format(label, unit))
    r.w("|---|--:|")
    for k, v in pairs:
        r.w("| {} | {} |".format(k, pc(v, total)))
    r.w("")
    if note:
        r.w("*{}*".format(note), "")


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
    # and why a day goes blank lives in usage_survey/README.md.

    # TWO CHARTS THIS SECTION NO LONGER DRAWS, and why. Daily active installs by
    # game and new installs per day by game plotted quantities the
    # new-versus-returning chart carries whole: the daily total is its stack,
    # new-per-day its lower band. The by-game split was near-degenerate --
    # MX Bikes is ~99% of launches, so its line WAS the total and the other
    # games were floor-huggers only visible on a log axis -- and three log
    # charts beside one linear chart of the same unit was a trap for anyone
    # comparing magnitudes across them. The Games table says what the minor
    # games are, in numbers; the sentence below says the one thing the curves
    # added. Everything here is linear.
    #
    # CUMULATIVE INSTALLS SEEN went with them once, on the argument that its
    # slope is new-per-day, which the band already shows. It is back, in front,
    # because the reader wants the LEVEL, not the slope: how many people have
    # downloaded, installed and actually run the thing, and how that grew. No
    # other chart shows a total rising; the Installs tile gives the number
    # without the story. One line, all games, linear.
    game_launch = ranked(started["game"]) if len(started) else pd.Series(dtype=int)
    if len(game_launch) > 1:
        minor = game_launch.iloc[1:]
        r.w("Curves pool all games ({} are {} of launches).".format(
                " and ".join(minor.index), pctstr(int(minor.sum()), len(started))), "")

    # Same one-game-per-install rule as the Games table: an install belongs to the
    # game it last reported under, so the handful that report under two games (the
    # id lives in the plugin's data folder, and a folder copied between games
    # carries it along) are not counted twice.
    dau_total = started.groupby("date")["install_id"].nunique()
    avg_active = avg_active_per_day(started, axis)
    # An install enters the population when it FIRST REPORTS, not when the user
    # installed the plugin. Analytics shipped in 1.26.0.0 (2026-06-28), so the
    # early ramp is existing users upgrading onto an instrumented build, not new
    # users -- hence the averages below take the last 30 FULL days.
    first_seen = started.groupby("install_id")["date"].min()
    new_total = first_seen.value_counts()
    # The first observed day after a collection gap carries the gap's BACKLOG (every
    # install that first ran during the outage is first seen that day), so it is a real
    # count and stays on the chart, but it is not a day's rate and stays out of the average.
    backlog_days = {days[b[1] + 1] for b in gap_bands if b[1] + 1 < len(days)}
    full_days = [d for d in days if d not in dark_days and d not in backlog_days]
    recent = full_days[-30:]

    # Running total of installs by the day they were first seen. Everything
    # first seen up to and including the day counts, partial edge days too (a
    # total is not a rate, so a part-day does not distort it); a dark day is a
    # gap in the line, and the first day after it steps up by the backlog.
    if recent:
        avg_new = sum(int(new_total.get(d, 0)) for d in recent) / len(recent)
        r.figures(("- **unique installs:** {:,}" + FIG_SEP +
                   "**avg new installs/day:** {:,.0f} (over the last {} full days)")
                  .format(len(first_seen), avg_new, len(recent)))
    if len(first_seen):
        firsts = first_seen.sort_values()
        cum, seen = [], 0
        idx = 0
        ordered = list(firsts)
        for d in days:
            while idx < len(ordered) and ordered[idx] <= d:
                idx += 1
            seen = idx
            cum.append(None if d in dark_days else seen)
        r.chart("activity_cumulative_installs.svg",
                svg.lines("Installs seen", xlabels,
                          [("All games", cum, svg.PALETTE[0])],
                          subtitle="running total of installs by the day each was first seen",
                          gaps=gap_bands, annotate="ends"),
                "Installs seen, cumulative")

    # NEW VERSUS RETURNING, of each day's active installs: the cut that says
    # whether a rise in new installs became a larger audience or churned straight
    # through it. Linear axis: the two are within a factor of ten.
    dau_all = started.groupby("date")["install_id"].nunique()
    returning = [None if d in dark_days else int(dau_all.get(d, 0)) - int(new_total.get(d, 0)) for d in days]
    fresh = [None if d in dark_days else int(new_total.get(d, 0)) for d in days]
    if recent:
        new_share = 100.0 * sum(int(new_total.get(d, 0)) for d in recent) / max(1, sum(int(dau_all.get(d, 0)) for d in recent))
        r.figures("- **share of a day's active installs that are new:** {:.0f}% (over the last {} full days)"
                  .format(new_share, len(recent)))
        r.chart("activity_new_vs_returning.svg",
                svg.lines("Active installs: new versus returning", xlabels,
                          [("Returning", returning, svg.PALETTE[1]), ("New (first day seen)", fresh, svg.PALETTE[0])],
                          subtitle="distinct installs launching per day, split by whether it was their first "
                                   "day; a day after a gap carries the gap's backlog of first days",
                          gaps=gap_bands),
                "Active installs, new versus returning")
        r.w("> The first weeks are existing users upgrading, not new ones; averages use the "
            "last {} full days.".format(len(recent)), "")

    # HOW HARD each of them leant on it, OVER TIME. This slot used to hold daily
    # launches, which was very nearly this chart's neighbour drawn again: launches
    # is active installs times a number that only wandered between 3.4 and 4.9
    # across the first window measured, so the two curves ran near-parallel and the
    # second taught almost nothing the first had not.
    #
    # The ratio is where the signal actually was, and no chart in the section
    # showed it: over that same window it drifted from ~4.4 to ~3.9, each active
    # install launching less than it had. Invisible in the population curve, too
    # subtle to read as a divergence between two near-parallel curves, and
    # flattened outright by the window-average bullet. Falling is not itself bad
    # news -- a growing base of newer, more casual installs dilutes intensity by
    # arithmetic alone -- but it is a fact about the plugin, and it was not on the
    # page.
    #
    # ONE line, all games together, unlike its two neighbours. A ratio does not
    # PARTITION the way a count does -- per-game ratios cannot be summed into the
    # total, so the sum assertion those two carry has nothing to check -- and a
    # game with a handful of active installs a day produces a ratio dominated by
    # sampling noise, which on a shared axis would set the y-scale and bury the
    # trend in the game that is 99% of the data.
    #
    # LINEAR axis, also unlike its neighbours: these are small ratios, not counts
    # spanning decades, so there is nothing for a log scale to compress and it
    # would only make an ordinary curve harder to read.
    launch_by_day = started.groupby("date").size()
    # avg over the SAME days the chart plots: `days` excludes partial edges, so
    # the numerator must not count launches on the part-days the denominator
    # dropped, or the figure rises ~6% for no real reason.
    full_day_launches = sum(int(launch_by_day.get(d, 0)) for d in plotted)
    if avg_active:
        # The curve's window average beside the total it is drawn from; the
        # curve itself is what shows the average moving.
        r.figures("- **avg launches per active install:** {:,.1f}{}"
                  "**avg launches/day:** {:,.0f}".format(
                      full_day_launches / (avg_active * len(plotted)),
                      FIG_SEP, full_day_launches / max(1, len(plotted))))
    intensity = []
    for d in days:
        act = int(dau_total.get(d, 0))
        intensity.append(None if (d in dark_days or act == 0)
                         else int(launch_by_day.get(d, 0)) / act)
    r.chart("activity_launches_per_active.svg",
            svg.lines("Launches per active install", xlabels,
                      [("All games", intensity, svg.PALETTE[0])],
                      subtitle="how many times a typical active install launched that day",
                      gaps=gap_bands),
            "Launches per active install")

    # WHEN PEOPLE RIDE: launches by weekday and hour, averaged per occurrence of
    # that weekday among the observed days, so a Tuesday that fell in an outage
    # does not read as a quiet Tuesday. UTC, because that is what the exporter
    # stamps; the players are mostly in Europe and the Americas, so read the
    # evening peak as spanning both. Hour buckets, which the rollup keeps.
    obs = started[started["date"].isin(set(plotted))]
    if len(obs):
        counts = obs.groupby([obs["ts"].dt.weekday, obs["ts"].dt.hour]).size()
        weekday_n = Counter(d.weekday() for d in plotted)
        grid = [[(int(counts.get((w, h), 0)) / weekday_n[w]) if weekday_n.get(w) else None
                 for h in range(24)] for w in range(7)]
        # The peak and the trough in words: a heatmap shows the shape, and a
        # reader still has to hunt for the one cell that answers "when".
        cells = [(v, w, h) for w, row in enumerate(grid) for h, v in enumerate(row) if v is not None]
        if cells:
            names = ("Mondays", "Tuesdays", "Wednesdays", "Thursdays", "Fridays", "Saturdays", "Sundays")
            peak = max(cells)
            by_hour = [[v for v, _w, hh in cells if hh == h] for h in range(24)]
            hour_avg = [(sum(v) / len(v), h) for h, v in enumerate(by_hour) if v]
            quiet = min(hour_avg)
            r.figures(("- **Busiest hour:** {} {:02d}:00 UTC, {:,.0f} launches" + FIG_SEP +
                       "**quietest hour of the day:** {:02d}:00 UTC, {:,.0f} launches").format(
                          names[peak[1]], peak[2], peak[0], quiet[1], quiet[0]))
        r.chart("activity_heatmap.svg",
                svg.heatmap("When people ride", ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"],
                            ["{:02d}".format(h) for h in range(24)], grid,
                            subtitle="average launches per hour of the week (UTC), over {} observed days"
                                     .format(len(plotted)), col_tick_every=3,
                            value_fmt=lambda v: "{:,.0f}".format(v)),
                "Launches by weekday and hour")

    # HOW LONG A SITTING LASTS. duration_seconds rides every session_end, but
    # session_end ITSELF was added in 1.27 -- 1.26 sent 83k launches and zero of
    # them, so a naive total silently covered ~36% of launches and read as a full
    # figure. Last in the section, after the three charts with a time axis, since
    # it is the one that has none; it used to head a section of its own called
    # Repeat usage, which promised retention and delivered a histogram.
    #
    # The under-a-minute bucket is split out and named: a launch that ends
    # inside a minute is a crash relaunch, a menu visit or a wrong server, not a
    # session, and unnamed it was the tallest bar on the page.
    dur = pd.to_numeric(sessions["_n"].map(lambda n: n.get("duration_seconds")),
                        errors="coerce")
    dur = dur[dur.notna() & (dur > 0)] / 60.0
    if len(dur):
        sbuckets = [("<1 min", 0, 1), ("1-5", 1, 5), ("5-15", 5, 15), ("15-30", 15, 30),
                    ("30-60", 30, 60), ("1-2 h", 60, 120), ("2 h+", 120, 10**9)]
        scats = [(lab, int(((dur >= lo) & (dur < hi)).sum())) for lab, lo, hi in sbuckets]
        # The total is a floor (every unreported session is missing from it), and
        # it is stated with the MEAN beside the median: sessions left open make a
        # long right tail, so the mean sits far above the median and a reader who
        # multiplies median by count gets less than half the total and thinks one
        # of the two is wrong. Hours, not days to five significant digits.
        r.figures(
            ("- **Median session:** {:.0f} min" + FIG_SEP + "**mean:** {:.0f} min" + FIG_SEP +
             "**{:,} sessions**").format(dur.median(), dur.mean(), len(dur)),
            "- **About {:,.0f} hours** in total; sessions left open pull the mean above the "
            "median.".format(round(dur.sum() / 60.0, -3)))
        r.chart("session_length.svg",
                svg.vbars("Session length", scats,
                          subtitle="sessions reporting a duration"),
                "Session length distribution")


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
# The crash table's small-base rule: a game under this floor (launches or
# installs) is flagged, since a few machines with one recurring fault can set
# its rate.
SMALL_CRASH_BASE = 100


def _versions(r, snap, started, axis, downloads=None):
    r.w("## Plugin version adoption", "")
    ch = snap["_s"].map(lambda s: s.get("update_channel"))
    ch = ch[ch.notna()]
    pre = int((ch == "prerelease").sum()) if len(ch) else 0
    r.w("Each install counts once, at its most recent version{}.".format(
        "; {:,} of {:,} run the prerelease channel".format(pre, len(ch)) if len(ch) else ""), "")
    vc = ranked(snap["app_version"])
    total = len(snap)
    main = [(v, int(c)) for v, c in vc.items() if c >= MIN_VERSION_INSTALLS]
    # ONE colour per version, shared with the migration chart below. Letting each
    # renderer assign palette slots by ITS OWN ordering -- installs here, launches
    # there -- puts the same version green in one and blue in the other, which
    # reads as two different versions rather than one.
    vercolor = {v: svg.PALETTE[i % len(svg.PALETTE)] for i, (v, _c) in enumerate(main)}
    famc = ranked(snap["app_version"].map(ver_family))
    _release_lines(r, famc, total, downloads)
    # MIGRATION OVER TIME. The release-line table above is a SNAPSHOT -- every
    # install at its latest version -- so it cannot show a rollout: an install that moved 1.26 -> 1.27
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
        vshare = ranked(day_last["app_version"])
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
                              value_fmt=lambda v: "{:.0f}%".format(v), gaps=axis["bands"],
                              annotate="legend"),
                    "Version migration over time")



def _release_lines(r, famc, total, downloads):
    """Installs and downloads per release line, in ONE table, newest line first.

    Ordered by VERSION, not by installs: with downloads beside them the table
    reads as the plugin's history, and 1.10 must sort above 1.9. Released is the
    line's first release. A cell with no
    number stays BLANK rather than reading 0 - lines before 1.26 predate the
    survey (downloads, no installs), and a build that was never published as a
    release has installs but no downloads.

    Installs keep the page's share-then-count shape; downloads are counts only,
    since a share of all-time downloads would weigh a line current for a month
    against one current for a week. The two are different units - the updater
    fetches every new version, so one install downloads many times - which the
    definition under the header tiles says, so the row is not read as a
    conversion rate."""
    inst = {str(f): int(c) for f, c in famc.items()}
    dl = downloads["lines"] if downloads else {}
    def key(f):
        return tuple(int(p) for p in re.findall(r"\d+", f))
    cols = (["Released"] if downloads else []) + ["Unique installs"] + \
        (["Downloads"] if downloads else [])
    r.w("| Release line | " + " | ".join(cols) + " |")
    r.w("|---|" + "|".join(["--:"] * len(cols)) + "|")
    for fam in sorted(set(inst) | set(dl), key=key, reverse=True):
        n, released = dl.get(fam, (None, None))
        cells = [pc(inst[fam], total) if fam in inst else ""]
        if downloads:
            cells = [released or ""] + cells + ["{:,}".format(n) if n is not None else ""]
        r.w("| {} | {} |".format(fam, " | ".join(cells)))
    tot = ["**{:,}**".format(total)]
    if downloads:
        tot = [""] + tot + ["**{:,}**".format(downloads["total"])]
    r.w("| **Total** | " + " | ".join(tot) + " |", "")


def _geography(r, snap):
    r.w("## Geography", "")
    cn = snap["country_name"].replace("", pd.NA).dropna()
    top = ranked(cn).head(15)
    if len(top):
        r.chart("geography.svg",
                svg.hbar("Installs by country (top 15)",
                         [(c, int(n), None, pc(n, len(cn))) for c, n in top.items()],
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

    b = ranked(osv.map(bucket))
    breakdown(r, "OS", "Installs", [(k, int(v)) for k, v in b.items()], len(osv),
              "{:,} of {:,} installs report an OS".format(len(osv), len(snap)))

    # Steam vs standalone lives here rather than under Games: it describes the RUNTIME
    # an install is under, which is the same question this section asks, not which game
    # it plays. Coverage-aware on steam_runtime, like every other adoption figure.
    # No Runtime table: it was two rows, one of them 99%, and the Highlights
    # Platform line already states the Steam share.


# Every achievement has the same four tiers (achievements.h kCatalogue), and the
# ping sends the tier reached as the value -- so a row is both "who holds this" and
# "how far in are they". A one-shot reports tier 1 and simply has no higher segments.
TIERS = 4
TIER_NAMES = ("Bronze", "Silver", "Gold", "Platinum")


def _achievements(r, snap):
    """How far installs get through the achievement catalogue, and which ones
    they hold (2.20.0 / 2.21.0 pings).

    The denominator throughout is installs that sent ach_pct AT ALL -- a version
    from before the feature carries none of these keys and is silent, not "0%".

    Its own section rather than a block inside Repeat usage: emitted mid-_engagement,
    its heading swallowed the session-length bullets that follow, which then read as
    achievement figures."""
    snap = snap[snap["app_version"].map(lambda v: ver_ge(v, *ACH_MIN))]
    ap = pd.to_numeric(snap["_n"].map(lambda n: n.get("ach_pct")), errors="coerce").dropna()
    if len(ap):
        r.w("## Achievements", "")
        buckets = [("0%", 0, 1), ("1–9%", 1, 10), ("10–24%", 10, 25), ("25–49%", 25, 50),
                   # "100%+" rather than "100%": a pre-2.23 ach_pct counted the
                   # hidden rows in its numerator against a total they were not
                   # in, so it could exceed 100. From 2.23 it is the listed set
                   # alone and tops out at exactly 100 (the surplus moved to
                   # ach_bonus), but historical rows still carry the old values.
                   ("50–74%", 50, 75), ("75–99%", 75, 100), ("100%+", 100, 10**9)]
        cats = [(lab, int(((ap >= lo) & (ap < hi)).sum())) for lab, lo, hi in buckets]
        # COUNTED FROM THE ROWS, not from ach_pct > 0. ach_pct is integer-truncated
        # over ~277 listed tiers, so one or two earned tiers floor to 0: 17 installs
        # read "0%" while holding something. And "past the first tier" invited three
        # different readings (earned anything / anything above Bronze / nonzero
        # percentage) that differ by ~40 installs, so the label now says which.
        held_any = int(snap["_n"].map(
            lambda n: any(k.startswith("ach_") and k not in _NOT_FLAGS for k in n)).sum())
        r.figures(("- **Achievement tiers earned, median per install:** {:.0f}%" + FIG_SEP +
                   "**installs holding at least one achievement:** {}"
                   ).format(ap.median(), adoption_annot(held_any, len(ap))))
        r.chart("achievement_progress.svg",
                svg.vbars("Achievement tiers earned per install", cats,
                          subtitle="of installs reporting achievements"),
                "Achievement progress")
        au = pd.to_numeric(snap["_n"].map(lambda n: n.get("ach_unlocked")), errors="coerce").dropna()
        if len(au):
            ub = [("0", 0, 1), ("1–4", 1, 5), ("5–9", 5, 10), ("10–24", 10, 25),
                  ("25–49", 25, 50), ("50+", 50, 10**9)]
            ucats = [(lab, int(((au >= lo) & (au < hi)).sum())) for lab, lo, hi in ub]
            r.figures("- **Achievements unlocked, median per install:** {:.0f}".format(
                au.median()))
            r.chart("achievements_unlocked.svg",
                    svg.vbars("Achievements unlocked per install", ucats,
                              subtitle="of installs reporting achievements"),
                    "Achievements unlocked")
        # Global achievement stats (2.21.0): the share of reporting installs that
        # hold each achievement, the way Steam lists them. A row is present in the
        # ping only at tier 1 or higher, so presence IS the unlock. Titles come
        # from docs/achievements.md, generated from the same catalogue the plugin
        # ships, so the labels here can only be as stale as that gate allows.
        reporting = snap[snap["_n"].map(lambda n: "ach_pct" in n)]
        # The ping's VALUE is the tier (1..4), not a flag, so the same rows answer
        # both "who holds this" and "how far in are they" -- which is the difference
        # between an achievement most people have and one most people have finished.
        counts = Counter()
        by_tier = defaultdict(Counter)
        for n in reporting["_n"]:
            for k, v in n.items():
                if k.startswith("ach_") and k not in _NOT_FLAGS:
                    counts[k[4:]] += 1
                    by_tier[k[4:]][max(1, min(TIERS, int(v)))] += 1
        cat = achievement_catalogue()
        # HIDDEN ROWS ARE NEVER NAMED IN THE OPEN. The plugin keeps a handful unlisted
        # until a player stumbles on them, and this report is public -- naming one in
        # the chart, or in the "rarest" line (where, rare by construction, they surface
        # first: it named Deja Vu before this filter existed), spoils exactly what they
        # are for. They get their own chart under a collapsed fold marked as spoilers
        # instead, so how players progress through them is still readable. The
        # per-install TOTALS keep them too, because that is what the plugin sent and an
        # earned hidden tier counts on top of the listed ones -- which is why the
        # progress chart has a bucket above 100%.
        listed = {i: v for i, v in cat.items() if not v["hidden"]}
        hidden_counts = {i: c for i, c in counts.items() if i in cat and cat[i]["hidden"]}
        counts = {i: c for i, c in counts.items() if i in listed}
        if counts:
            base = len(reporting)

            def label(i):
                return cat.get(i, {}).get("title") or _pretty_key(i)

            def icon(i):
                return icon_geometry(cat.get(i, {}).get("icon"))

            def reqs(i):
                return ((cat.get(i, {}).get("tiers") or []) + [None] * TIERS)[:TIERS]

            def one_shot(i):
                return not any(reqs(i)[1:])

            def segments(i, by):
                # Each tier carries its requirement, worded as the toast words it,
                # for the Pages copy's hover readout ("Racer: Silver 20% (2,868)" /
                # "Finish 10 races"). A one-shot has no tiers to name, so its one
                # segment reads "Rule Bender: 18% (2,581)", not "Bronze 18%".
                shot = one_shot(i)
                return [("t{}".format(t), by[i][t], reqs(i)[t - 1]) + (("",) if shot else ())
                        for t in range(1, TIERS + 1)]

            def about(i):
                # The row's label reads out what the achievement asks: its one
                # requirement, or the whole ladder, tier by tier.
                if one_shot(i):
                    return reqs(i)[0]
                return "\n".join("{}: {}".format(TIER_NAMES[t], req) for t, req in enumerate(reqs(i)) if req)

            # Only rows somebody holds. Charting the whole catalogue put a fifth of it
            # on the page as empty bars saying nothing but "the counter started
            # yesterday" -- the count below carries that fact in one line instead.
            ranked = sorted(counts, key=lambda i: (-counts[i], label(i)))
            # The hidden clause is not trivia: it is why this says 85 and not 93, and
            # why the progress chart above has a bucket past 100%.
            # No "rarest" line: the chart is ranked, so the rarest are its last rows.
            r.figures(
                "- **Earned so far:** {:,} of the {:,} listed achievements; the {:,} hidden "
                "ones are under the fold below.".format(
                    len(counts), len(listed), len(cat) - len(listed)))
            r.chart("achievements_global.svg",
                    svg.stacked_hbar(
                        "Achievements by tier reached",
                        [(label(i), icon(i), segments(i, by_tier),
                          # pc(), not a raw %: the tail is visible now that every listed
                          # row is charted, and ~15 rows sit under 1%. Formatted plainly
                          # they all read "0%", so an achievement two people hold looks
                          # exactly like one nobody has. pctstr's "<1%" is the report's
                          # convention for precisely this.
                          pc(counts.get(i, 0), base), about(i))
                         for i in ranked],
                        [("t{}".format(t), TIER_NAMES[t - 1]) for t in range(1, TIERS + 1)], base,
                        subtitle="% of installs reporting achievements; bar length is "
                                 "how many hold it, the split is how far they have taken it"),
                    "Achievements by tier reached")
            if hidden_counts:
                hranked = sorted(hidden_counts, key=lambda i: (-hidden_counts[i], label(i)))
                r.w("<details>",
                    "<summary><b>Hidden achievements</b> - {} of the {} earned by anyone (spoilers)</summary>"
                    .format(len(hidden_counts), len(cat) - len(listed)), "")
                r.chart("achievements_hidden.svg",
                        svg.stacked_hbar(
                            "Hidden achievements by tier reached",
                            [(label(i), icon(i), segments(i, by_tier),
                              pc(hidden_counts.get(i, 0), base), about(i))
                             for i in hranked],
                            [("t{}".format(t), TIER_NAMES[t - 1]) for t in range(1, TIERS + 1)], base,
                            subtitle="% of installs reporting achievements; same reading as the "
                                     "chart above"),
                        "Hidden achievements by tier reached", html=True)
                r.w("</details>", "")


def _features(r, snap, started=None):
    # HUD/widget/feature availability is game-specific (e.g. ECU & Tyre Temp are
    # GP Bikes only, FMX/Records are MX Bikes only), and the plugin only emits a
    # flag where that HUD/widget exists. Pooling games would put a GP-Bikes-only
    # widget's rate next to MX-Bikes rates over wildly different bases, so adoption
    # is shown for the primary (most-installed) game - 98%+ of the base - where
    # every flag shares one denominator.
    gc = ranked(snap["game"])
    primary = gc.index[0]
    psnap = snap[snap["game"] == primary]
    others = ["{} {:,}".format(g, int(c)) for g, c in gc.items() if g != primary]

    r.w("## Feature & HUD adoption - {}".format(primary), "")
    intro = "{} only ({:,} installs).".format(primary, len(psnap))
    if others:
        intro += " *({} have too few for their own.)*".format(" and ".join(others))
    r.w(intro, "")
    # Counts of enabled HUDs/widgets per install (primary game). Above the charts
    # it frames, not stranded at the bottom of the section under the last of them.
    hc = pd.to_numeric(psnap["_n"].map(lambda n: n.get("hud_count")), errors="coerce").dropna()
    wc = pd.to_numeric(psnap["_n"].map(lambda n: n.get("widget_count")), errors="coerce").dropna()
    if len(hc):
        r.figures(("- **Median HUDs enabled per install:** {:.0f}" + FIG_SEP +
                   "**median widgets:** {:.0f}").format(
                       hc.median(), wc.median() if len(wc) else 0))

    def render(title, prefix, name, labeler):
        rows = flag_adoption(psnap, prefix)
        skipped = [k for k, _en, _rep in rows if k in NOT_CHARTED]
        rows = [row for row in rows if row[0] not in NOT_CHARTED]
        if not rows:
            return
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

        bars = [(labeler(k), pct(en, rep) if rep else 0, None, adoption_annot(en, rep))
                for k, en, rep in rows]
        r.chart(name,
                svg.hbar(title, bars,
                         subtitle="% of {} installs reporting each flag".format(primary),
                         value_fmt=lambda v: "{:.0f}%".format(v)),
                title)
        if thin:
            r.w("*Too few reports to rank (under {} installs): {}.*".format(
                MIN_ADOPTION_BASE,
                ", ".join("{} {:,} of {:,}".format(labeler(k), en, rep)
                          for k, en, rep in sorted(thin, key=lambda t: -t[2]))), "")
        if skipped:
            r.w("*Not charted: {}.*".format("; ".join(
                "{} ({})".format(labeler(k), NOT_CHARTED[k]) for k in sorted(skipped))), "")

    psnap = derive_label_flags(psnap)   # theme and spotter on/off rank with the sent flags
    render("Features (feat_*)", "feat_", "features.svg",
           lambda k: FEATURE_LABELS.get(k, _pretty_key(k)))
    render("HUD adoption (hud_*)", "hud_", "huds.svg", _pretty_key)
    render("Widget adoption (widget_*)", "widget_", "widgets.svg", _pretty_key)



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
    affected = cr["install_id"].nunique()
    base_installs = stmin["install_id"].nunique()
    r.w("Analysed: **{:,}** of **{:,}** crash reports ({}+ builds, full diagnostics). "
        "Excluded: {:,} from earlier builds and {:,} from dev tooling.".format(
            len(cr), len(crashes), CRASH_MIN_STR, pre_n, dev_n), "")
    # ONE headline, per install: MX Bikes is ~99% of launches, so an overall
    # per-launch rate was that table's MX Bikes row stated twice.
    r.figures(
        "- **Affected installs:** {:,} of {:,} running {}+ ({}) sent at least one crash "
        "report".format(affected, base_installs, CRASH_MIN_STR, pctstr(affected, base_installs)))
    r.w("> Recorded faults landed **outside the plugin** (in the game or another module), "
        "not necessarily the root cause. [Known crashes]({}) are grouped by cause, the rest "
        "by module.".format(KGC), "")

    # by game (1.27.5+)
    r.w("### Crash rate by game", "")
    r.w("| Game | Installs | {}+ launches | Crash reports | Launches with a crash report |".format(CRASH_MIN_STR))
    r.w("|---|--:|--:|--:|--:|")
    small = []   # "<game> has N installs", for the footnote
    for g in sorted(set(cr["game"]) | set(stmin["game"])):
        gc = int((cr["game"] == g).sum())
        gl = int((stmin["game"] == g).sum())
        gi = int(stmin[stmin["game"] == g]["install_id"].nunique())
        if gl or gc:
            rate = "{:.1f}%".format(pct(gc, gl))
            # Too few LAUNCHES for a stable rate, or too few INSTALLS for it to be
            # a population: a game with 47 installs can post an alarming rate off
            # two or three machines with one recurring fault, and a launch count
            # alone does not say so. The footnote names which base tripped it,
            # so thousands of launches beside the asterisk do not read as a slip.
            why = []
            if gi < SMALL_CRASH_BASE:
                why.append("{:,} installs".format(gi))
            if gl < SMALL_CRASH_BASE:
                why.append("{:,} launches".format(gl))
            if why:
                rate += " *"
                small.append("{} has {}".format(g, " and ".join(why)))
            r.w("| {} | {:,} | {:,} | {:,} | {} |".format(g, gi, gl, gc, rate))
    if small:
        r.w("", "*\\* {}: a few machines can set this rate.*".format("; ".join(small)))
    r.w("")

    # NO PER-VERSION CRASH TABLE. It was drawn for a while (1.27.5+ rows, a
    # 2,000-launch floor, oldest first) and dropped as the author's call: the
    # crashes are the game's, not the plugin's, so a per-release rate invited
    # reading a game patch or a hardware shift as a plugin regression. The
    # by-game table above is the denominator that matters.
    # Resolve each crash to a catalogued crash (or None) FIRST: a catalogued crash
    # is categorised by the cause the catalogue records, and only an uncatalogued
    # one by its faulting module (see known_category).
    # apply() over an EMPTY frame hands back a frame, not a Series, and the
    # assignment then fails -- an export with no crash events (a fresh app, a
    # synthetic one) must still produce a report.
    cr["known"] = (cr.apply(lambda row: match_known(row["fault"], row["game_build"], lookup), axis=1)
                   if len(cr) else pd.Series(dtype=object))
    cr["module"] = cr["fault"].map(lambda f: f.split("+")[0] if f else "unknown")
    cr["category"] = [known_category(k) if k else categorize_module(m)
                      for k, m in zip(cr["known"], cr["module"])]
    catc = ranked(cr["category"])
    r.chart("crash_categories.svg",
            svg.hbar("Crashes by cause",
                     [(k, int(v), None, pc(v, len(cr))) for k, v in catc.items()],
                     subtitle="catalogued crashes by recorded cause, the rest by faulting module",
                     label_w=170),
            "Crashes by category")
    plug_n = int(catc.get("Plugin (MXBMRP3)", 0))
    if plug_n:
        r.w("*The Plugin (MXBMRP3) slice ({} crash{}) is where the fault landed, not proof "
            "the plugin caused it.*".format(
                plug_n, "" if plug_n == 1 else "es"), "")

    # The fault's own shape. Prose here and a bullet for every other rate in the
    # section was the same statement in two containers; both are breakdowns.
    # BOTH TABLES OVER len(cr), the reliable crash set, and both carrying their
    # remainder. They sat side by side over different bases -- av_type over the
    # rows that HAVE one, codes over every report, top four only -- so neither
    # summed to the whole and the two could not be read against each other. A
    # share is meaningless without saying share of what, and adjacency promises
    # they are the same what.
    av = cr["av"].dropna()
    if len(av):
        rows = [(str(k), int(v)) for k, v in ranked(av).items()]
        missing = len(cr) - len(av)
        if missing > 0:
            rows.append(("not recorded", missing))
        breakdown(r, "Access-violation type", "Reports", rows, len(cr))
    # No exception-code table: one code (access violation) is over 99% of it,
    # and the access-violation split above is the informative half.

    # ONE ranked table per unit: named crashes by name (with workaround), and the
    # uncatalogued tail by fault signature - so no crash is listed twice.
    matched = int(cr["known"].notna().sum())

    by_known = defaultdict(int)
    kmeta = {}
    for _, row in cr[cr["known"].notna()].iterrows():
        k = row["known"]
        by_known[k["id"]] += 1
        kmeta[k["id"]] = k

    r.w("### Most common crashes", "")
    r.w("**{}** of reports match the [known-crash list]({}).".format(
            pc(matched, len(cr)), KGC), "")
    if by_known:
        r.w("| Crash | Share | Fix / workaround |")
        r.w("|---|--:|:--:|")
        for cid, cnt in sorted(by_known.items(), key=lambda x: (-x[1], x[0])):
            k = kmeta[cid]
            wk = "[✔]({})".format(KGC) if k.get("workaround") else "-"
            r.w("| [{}]({}) | {} | {} |".format(k["name"], KGC, pc(cnt, len(cr)), wk))
        r.w("")
        r.w("*✔ = a documented workaround.*", "")

    # Uncatalogued tail - by fault signature, for whoever extends the catalogue.
    unc = cr[cr["known"].isna()]
    if len(unc):
        sig = ranked(unc["fault"])
        r.w("", "### Not yet catalogued", "")
        r.w("The other **{}**, most frequent first{}".format(
                pc(len(unc), len(cr)),
                ". `unknown+0x0` is an address in no loaded module (likely freed memory)."
                if "unknown+0x0" in sig.index else "."), "")
        r.w("| Fault (module + offset) | Category | Share |")
        r.w("|---|---|--:|")
        for fault, n in sig.head(10).items():
            r.w("| `{}` | {} | {} |".format(fault, categorize_module(fault.split("+")[0]), pc(n, len(cr))))
        r.w("")


def _coverage(r, started, snap, dev_installs=0, dev_events=0,
              unidentified=0, unidentified_events=0, unidentified_machines=0):
    r.w("## About this data", "")
    r.w("The plugin only sends anonymous, aggregate telemetry (no personal data - see the "
        "privacy note in the main README). Reporting began in **1.26** (a few stray older "
        "builds send only a basic launch event) and expanded through **1.27**, so older "
        "versions report fewer fields. Percentages are always taken over the installs that "
        "actually report a given field, and each chart notes how many installs that is, so "
        "incomplete rollout is never shown as a real trend.", "")
    r.w("**Definitions.** *Unique install* - a unique `install_id` (regenerated if the analytics "
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
    if unidentified:
        # Every figure here is DERIVED, not typed: this file regenerates daily, so
        # a literal "a fifth" and "under 1%" would be true on the day they were
        # written and quietly wrong after. Shares are of what the totals WOULD
        # have been, which is the only denominator the sentence can mean.
        would_installs = unidentified + int(snap["install_id"].nunique())
        would_events = unidentified_events + len(started)
        # What these are is now known, and the sentence says so: real installs
        # whose analytics file had become unreadable (damaged, most likely by a
        # 1.26 build's non-atomic write), so the plugin minted a throwaway id on
        # EVERY launch and never repaired the file. The plugin now repairs it on
        # the spot, so the count here falls as those installs update; the
        # machine floor is what keeps the install-share figure from reading as
        # a fifth of the audience when it is a few dozen PCs.
        r.w("**Unidentified launches are excluded report-wide** ({:,} of them, {} of what "
            "the install total would otherwise be, from at least {:,} machine{}): a plugin "
            "whose analytics file had become unreadable minted a throwaway id on every "
            "launch, so each launch arrived as a brand-new install that never returned. "
            "Their launches go with them ({} of launches). The plugin now repairs the "
            "file, so this shrinks as those installs update.".format(
                unidentified, pctstr(unidentified, would_installs),
                unidentified_machines, "" if unidentified_machines == 1 else "s",
                pctstr(unidentified_events, would_events)), "")
    started = started.copy()
    started["fam"] = started["app_version"].map(ver_family)
    # Read off the `cov` mask load() computed, not the props: a launch already in the
    # rollup no longer carries them, and asking each row for its props would quietly
    # report 0% for every month that had aged into the digest. Crash detail stays a
    # property of the VERSION, so it needs nothing stored.
    fields = [
        ("Features", lambda sub: (sub["cov"] & rollup.COV_FEATURES) != 0),
        ("HUDs / widgets", lambda sub: (sub["cov"] & rollup.COV_HUDS) != 0),
        ("OS version", lambda sub: (sub["cov"] & rollup.COV_OS) != 0),
        ("Update channel", lambda sub: (sub["cov"] & rollup.COV_CHANNEL) != 0),
        ("Panel theme", lambda sub: (sub["cov"] & rollup.COV_THEME) != 0),
        ("Crash detail", lambda sub: sub["app_version"].map(lambda v: ver_ge(v, *CRASH_MIN))),
    ]
    fams = sorted(started["fam"].unique())
    r.w("What each release line reports (share of its launches):", "")
    r.w("| Release line | Launches | " + " | ".join(n for n, _ in fields) + " |")
    r.w("|---|--:|" + "|".join(["--:"] * len(fields)) + "|")
    for fam in fams:
        sub = started[started["fam"] == fam]
        if not len(sub):
            continue
        cells = ["{:.0f}%".format(100 * fn(sub).mean()) for _, fn in fields]
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
             "steam_runtime": 1, "install_age_days": 0,
             # 2.20.0 / 2.21.0: the two totals and two earned rows by id
             # deja_vu is Group::Hidden: earned here, named ONLY under the spoiler fold.
             "ach_pct": 12, "ach_unlocked": 2, "ach_races": 2, "ach_config_reloads": 1,
             "ach_deja_vu": 1}
    # panel_theme rides on install-1 only, so the theme chart's denominator is the
    # REPORTING installs (1) rather than all of them (2) -- the same coverage-aware
    # shape the feat_* flags have, and the state the world is actually in while
    # older builds are still out there.
    ev("app_started", base, "userA1", "install-1",
       sp={"panel_theme": "carbon-dark", "spotter": "default", "_ver": "1.30.1.56"}, npr=flags)
    ev("app_started", base + 86400, "userA2", "install-1",
       sp={"panel_theme": "carbon-dark", "spotter": "default", "_ver": "1.30.1.56"}, npr=flags)
    ev("session_end", base + 100, "userA1", "install-1", npr={"duration_seconds": 600})
    # A minimal (early-build) launch with NO feature flags -> coverage must exclude it.
    ev("app_started", base + 200, "userB1", "install-2",
       sp={"_ver": "1.26.0.0", "_os": ""}, npr={"launch_count": 1, "install_age_days": 5})
    # A player crash (1.27+, reliable) that matches a catalogued known crash.
    ev("crash", base + 300, "userA1", "install-1",
       sp={"host": "mxbikes.exe", "fault": "mxbikes.exe+0x1f1923", "crash_plugin_version": "1.27.5.43",
           "game_build": "0x6A21833D", "code": "0xC0000005", "av_type": "read"})
    # FIVE MORE reliable crashes on the same install, shaped to exercise the two
    # REMAINDER rows the crash breakdowns carry: six distinct exception codes, so
    # the top-four table needs an "other"; and one crash with no av_type, so the
    # access-violation table needs a "not recorded". Both tables divide by the
    # whole reliable set, and a remainder is the only thing making them sum to it
    # -- without these the rows were unreachable and the sum went unchecked.
    # All on install-1 (already affected, so the affected-installs count holds)
    # and all in mxbikes.exe (so none lands in the Plugin slice asserted absent).
    for i, (code, av) in enumerate([("0xC0000006", "write"), ("0xC0000007", "execute"),
                                    ("0xC0000008", "read"), ("0xC000000A", "read"),
                                    ("0xC0000009", None)]):
        sp = {"host": "mxbikes.exe", "fault": "mxbikes.exe+0x{:x}".format(0x900000 + i),
              "crash_plugin_version": "1.27.5.43", "game_build": "0x6A21833D", "code": code}
        if av:
            sp["av_type"] = av
        ev("crash", base + 310 + i, "userA1", "install-1", sp=sp)

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

    # A 1.30.0 install: its ping was assembled before the save file loaded, so it
    # reports the achievement TOTALS as a hard zero and no earned rows. It must not
    # land in the achievement denominators as a player who has earned nothing.
    ev("app_started", base + 250, "userC1", "install-3", sp={"_ver": "1.30.0.55"},
       npr={"launch_count": 3, "install_age_days": 40,
            "ach_pct": 0, "ach_unlocked": 0, "feat_overlay": 0,
            "hud_map": 0, "widget_speed": 0, "hud_count": 0, "widget_count": 0})

    # Two launches in the SAME HOUR with an upgrade between them. The version-migration
    # chart credits an install to the last version it ran that day, so the pair has to
    # stay the right way round -- which an hour-resolution digest only manages because
    # it stores their order (see `seq` in analytics_rollup.py).
    # A, then B, then A again -- somebody switching builds -- and the versions are
    # 1.29.10 / 1.29.9, which sort the OTHER way round as text (".10" < ".9"). Both
    # halves matter: an ordinary A-then-B pair is reproduced by accident, because the
    # digest's own rows come out in version order and happen to end on the right one,
    # and an A/B/A run is what a per-row sequence number still gets wrong.
    ev("app_started", base + 60, "userE1", "install-4", sp={"_ver": "1.29.10.50"},
       npr={"launch_count": 7})
    ev("app_started", base + 120, "userE1", "install-4", sp={"_ver": "1.29.9.49"},
       npr={"launch_count": 8})
    ev("app_started", base + 180, "userE1", "install-4", sp={"_ver": "1.29.10.50"},
       npr={"launch_count": 9, "install_age_days": 70})

    # An UNIDENTIFIED launch: the analytics file was there but unreadable, so the
    # plugin used a throwaway id and reported launch_count 0 (a genuine first run
    # reports 1 -- see the exclusion in build()). It must not count as an install.
    ev("app_started", base + 800, "userF1", "install-unreadable",
       sp={"_ver": "1.28.0.48"}, npr={"launch_count": 0, "install_age_days": 0,
                                      "feat_overlay": 1, "hud_map": 1})

    # A developer/test install -> MUST be dropped report-wide (launch + test crash).
    dev_id = next(iter(DEV_INSTALL_IDS))
    ev("app_started", base + 600, "userD1", dev_id, npr=flags)
    ev("crash", base + 700, "userD1", dev_id,
       sp={"host": "mxbikes.exe", "fault": "mxbmrp3.dlo+0xdead", "crash_plugin_version": "1.27.5.43",
           "game_build": "0x6A21833D", "code": "0xC0000005", "av_type": "write"})

    def mkdf(row_list):
        return derive(pd.DataFrame(row_list))

    # Direct-helper invariants on the core fixture (no dev install).
    core = mkdf(core_rows)
    snap = latest_per_install(core[core.event_name == "app_started"])
    assert snap["install_id"].nunique() == 2, "install_id should collapse rotating user_ids"
    assert core["user_id"].nunique() == 3, "sanity: 3 rotating user_ids in fixture"
    # Two launches inside ONE second: the snapshot must be the later of them (higher
    # launch_count), not whichever row the frame happens to end on.
    tied = mkdf([dict(core_rows[0], timestamp=base + 9000, user_id="userT",
                      session_id="userT-s",
                      string_props=json.dumps({"install_id": "install-t", "game": "MX Bikes"}),
                      numeric_props=json.dumps({"launch_count": lc}))
                 for lc in (2, 1)])   # deliberately the wrong way round in the frame
    assert latest_per_install(tied)["_n"].iloc[0]["launch_count"] == 2, \
        "a timestamp tie must resolve to the later launch, not to frame order"

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
    # A renamed game executable is still the game: matched on the stem, joined by
    # build hash like the stock name.
    assert categorize_module("mxbikesJ.exe") == "Game", "renamed game exe must classify as Game"
    assert categorize_module("mxbikes.dll") != "Game", "only an .exe is the game module"
    assert match_known("mxbikesJ.exe+0x1f1923", "0x6A21833D", lookup), \
        "renamed game exe must join the catalogue by build hash"
    # A catalogued crash is categorised by the cause the catalogue records, not by
    # the module the fault landed in.
    trk = match_known("msvcr90.dll+0x36ede", None, lookup)
    assert trk and known_category(trk) == "Game", \
        "track-load crash faults in the CRT but is a game crash"
    assert categorize_module("msvcr90.dll") == "System / runtime"
    obs = match_known("ntdll.dll+0xb9d3", None, lookup)
    assert obs and known_category(obs) == "Third-party (OBS)", "third_party entry names its owner"

    # pctstr caps: '100%' only for the literal whole; near-whole reads '>99%'.
    assert pctstr(1000, 1000) == "100%"
    assert pctstr(999, 1000) == ">99%"
    assert pctstr(3, 1000) == "<1%"

    # Full pipeline including the dev install, which build() must drop report-wide.
    out = tempfile.mkdtemp(prefix="analytics_selftest_")
    with open(os.path.join(out, "downloads.json"), "w") as f:
        json.dump({"repo": "x", "releases": [
            {"tag": "v1.9.2.0", "published": "2025-12-23", "downloads": 5},
            {"tag": "v1.30.3", "published": "2026-09-13", "downloads": 100},
            {"tag": "v1.30.1", "published": "2026-09-06", "downloads": 20},
            {"tag": "v1.10.0.0", "published": "2025-12-29", "downloads": 7}]}, f)
    path, _ = build(mkdf(rows), out)
    md = open(path).read()
    # 1 pre-threshold + 1 dev-host crash excluded, leaving exactly 1 counted player crash;
    # the dev install's launch + test crash must not appear.
    assert "1 from earlier builds and 1 from dev tooling" in md, "crash exclusions not reported"
    assert "**Affected installs:** 1 of" in md, "dev-install test crash not excluded"
    assert "Developer/test machines are excluded" in md, "dev-install exclusion not reported"
    # The unreadable-file launch is out of every install figure, and said so.
    # Testing `launch_count == 0` on a MISSING field would delete real installs
    # from builds predating telemetry schema 2.2.0, so the predicate is
    # present-and-zero.
    assert "Unidentified launches are excluded report-wide** (1 of them," in md, \
        "the throwaway-id launch must be excluded and disclosed"
    # The TILE, not the id: ids are never printed, so asserting one is absent can
    # never fail. This is what actually moves if the snapshot-side filter is
    # dropped -- the disclosure count is derived from the event frame and would
    # still read 1 while every install figure counted the phantom again.
    assert "| Unique installs | **4** |" in md, \
        "install tile must count 4 real installs, with the unidentified launch out"
    assert "Plugin (MXBMRP3)" not in md, "no plugin-module crash should survive dev exclusion"
    assert "About this data" in md
    assert os.path.exists(os.path.join(out, "charts", "crash_categories.svg"))
    # The access-violation breakdown carries its remainder, so it sums to the
    # whole. Asserted on the row only a remainder can produce: it did not exist
    # while the fixture had a single crash, which is how it shipped unpinned.
    assert "| not recorded |" in md, \
        "a crash with no av_type must appear as the access-violation remainder"
    # The achievements section: both totals charted, and the per-achievement
    # share with the catalogue's titles (Racer for ach_races) rather than raw ids.
    assert "## Achievements" in md, "achievements section missing"
    assert os.path.exists(os.path.join(out, "charts", "achievement_progress.svg"))
    assert os.path.exists(os.path.join(out, "charts", "achievements_unlocked.svg"))
    assert os.path.exists(os.path.join(out, "charts", "achievements_global.svg"))
    glob_svg = open(os.path.join(out, "charts", "achievements_global.svg")).read()
    assert "Racer" in glob_svg, \
        "achievement chart should label rows by title from docs/achievements.md"
    # Each tier segment's hover readout names what that tier takes, worded as the
    # catalogue words it (the fixture's Racer sits at tier 2: "Finish 10 races").
    assert "&#10;Finish 10 races" in glob_svg, \
        "an achievement tier's hover readout lost its requirement"
    # EVERY HOVER READOUT IS "share (count)", the shape of the labels beside it.
    # The tier segments used to read a bare share ("Bronze 5%") and the histograms
    # a bare count ("0%: 1,052"), so the reader got half the pair on hover.
    assert re.search(r'<rect [^>]*data-tip="Racer: \w+ (<1|\d+)% \([\d,]+\)', glob_svg), \
        "an achievement tier's hover readout lost its share or count"
    prog_svg = open(os.path.join(out, "charts", "achievement_progress.svg")).read()
    tips = re.findall(r'<rect [^>]*data-tip="([^"]*)"', prog_svg)
    assert tips and all(re.fullmatch(r'.+: (<1|>99|\d+)% \([\d,]+\)', t) for t in tips), \
        "a histogram bar's hover readout is not share (count): {}".format(tips)
    # The row's name reads out the whole ladder.
    assert 'class="lbl" data-tip="Racer&#10;Bronze: Finish a race&#10;Silver: Finish 10 races' in glob_svg, \
        "an achievement's name lost its requirement readout"
    # HIDDEN ACHIEVEMENTS ARE NEVER NAMED. They are rare by construction, so the
    # "rarest" line is where they surface first -- it named Deja Vu in a published
    # report before this filter existed. The catalogue check is the other half: the
    # flag is read off the doc's group HEADINGS, so renaming one would quietly turn
    # the filter into a no-op and start publishing them again. Asserted per group, so
    # renaming ONE of the two is caught -- a combined count would stay non-empty and
    # pass while half the rows became publishable.
    cat_for_hidden = achievement_catalogue()
    for grp in HIDDEN_GROUPS:
        ids = [i for i, v in cat_for_hidden.items() if v["hidden"] and v["group"] == grp]
        assert ids, "no rows found under '## {}' - has the section been renamed? " \
                    "its rows would now be publishable".format(grp)
    assert "Deja Vu" not in glob_svg, "a hidden achievement must not be charted in the open"
    # Named ONLY inside the chart under the spoiler fold: the markdown itself never
    # carries the title (the chart is an image), and the image sits between the
    # <details> tags so it is collapsed until a reader asks for it.
    assert "Deja Vu" not in md, "a hidden achievement must not be named in the report text"
    hid_svg = open(os.path.join(out, "charts", "achievements_hidden.svg")).read()
    assert "Deja Vu" in hid_svg, "the hidden chart under the fold must show the earned hidden row"
    assert "<details>" in md and "</details>" in md and \
        md.index("<details>") < md.index("achievements_hidden.svg") < md.index("</details>"), \
        "the hidden chart must sit inside the spoiler fold"

    # GPU rendering is counted only over installs running the companion window it
    # drives: over everyone, a default-on setting read 100% of people who never
    # opened the window.
    fa_gpu = {k: (en, rep) for k, en, rep in flag_adoption(
        {"_n": [{"feat_hwaccel": 1, "feat_companion": 1}, {"feat_hwaccel": 0, "feat_companion": 1},
                {"feat_hwaccel": 1, "feat_companion": 0}, {"feat_hwaccel": 1}]}, "feat_")}
    assert fa_gpu["feat_hwaccel"] == (1, 2), "GPU rendering must be over companion users only: {}".format(fa_gpu)
    # The always-on widgets are named under the chart, never ranked in it.
    assert set(NOT_CHARTED) == {"widget_pointer", "widget_spotter"}

    # ONE ORDER for a count with its share, everywhere: "46% (1,606)", never
    # "1,606 (46%)". Checked over the whole page and every chart label, since the
    # two orders are each correct alone and only look wrong side by side.
    everything = md + "".join(open(f, encoding="utf-8").read()
                              for f in glob.glob(os.path.join(out, "charts", "*.svg")))
    backwards = re.findall(r"\b\d[\d,]* \((?:&lt;|&gt;|[<>])?\d+%\)", everything)
    assert not backwards, "count-first share(s) found, use pc(): {}".format(backwards[:5])

    # One shape for every adoption bar, a full base and a newer flag's small one
    # alike: share, then count OF its own base.
    assert adoption_annot(11037, 11210) == "98% (11,037 of 11,210)", adoption_annot(11037, 11210)
    assert adoption_annot(2868, 2868) == "100% (2,868 of 2,868)"

    # THE PAGES COPY. index.html is written beside REPORT.md from the same text, with
    # EVERY chart inlined (so its hover script can reach it) and none left as an
    # <img> -- one missed would be a dead picture on the page. The hover itself is
    # driven in a browser by tests/web/tests/usage_page.spec.js.
    html_page = open(os.path.join(out, "index.html"), encoding="utf-8").read()
    n_charts = len(glob.glob(os.path.join(out, "charts", "*.svg")))
    md_refs = len(re.findall(r"charts/[\w.-]+\.svg", md))
    assert html_page.count("<svg ") == n_charts == md_refs, \
        "index.html inlines {} charts; REPORT.md references {} and {} were written".format(
            html_page.count("<svg "), md_refs, n_charts)
    assert 'src="charts/' not in html_page, "a chart was left as an <img> on the Pages copy"
    assert "data-chart=" in html_page and "data-tip=" in html_page, "the charts lost their hover data"

    # THE NEW ACTIVITY CUTS. Produced and referenced, like every other chart; and the
    # heatmap has one cell per hour of the week whether or not that hour was observed.
    for name in ("activity_new_vs_returning.svg", "activity_heatmap.svg"):
        assert os.path.exists(os.path.join(out, "charts", name)), name + " missing"
        assert name in md, name + " produced but REPORT.md does not reference it"
    heat = open(os.path.join(out, "charts", "activity_heatmap.svg")).read()
    assert heat.count("<rect") == 7 * 24, "the heatmap must draw every hour of the week"
    # The adoption-speed table and the feature-by-cohort chart are gone by
    # decision: six of ten rows were dashes, and the cohort chart drew two honest
    # cohorts as a trend.
    assert "| Version | First seen |" not in md, "the adoption-speed table is back"
    assert not os.path.exists(os.path.join(out, "charts", "features_by_cohort.svg")), \
        "features_by_cohort.svg is back"
    # ONE TITLE PER CHART: the SVG names itself (a <title> and an aria-label, so
    # it is not a nameless image to a screen reader) and the Markdown alt is that
    # title verbatim -- a renderer that captions from alt printed nineteen
    # captions disagreeing with the picture under them.
    for _svg_name in sorted(os.listdir(os.path.join(out, "charts"))):
        _txt = open(os.path.join(out, "charts", _svg_name)).read()
        _t = re.search(r"<title>(.*?)</title>", _txt)
        assert _t and _t.group(1), _svg_name + " has no <title>"
        assert 'aria-label="' + _t.group(1) + '"' in _txt, _svg_name + " title and aria-label differ"
        assert ("![{}](charts/{})".format(html_unescape(_t.group(1)), _svg_name) in md or
                'alt="{}"'.format(html_unescape(_t.group(1))) in md), \
            _svg_name + ": the Markdown alt is not the chart's own title"
    # The crash section's headline, what remains after the per-launch overall
    # rate (the MX Bikes row twice) went; the per-version table went too, as
    # the author's call (see _crashes), so it must not creep back.
    assert "### Crash rate by plugin version" not in md, \
        "the per-version crash table is back - the crashes are the game's, not the plugin's"
    assert "**Crash-report rate:**" not in md, "the overall per-launch rate is the MX Bikes row twice"
    assert "**Affected installs:**" in md, "the per-install stability headline is missing"
    assert "| Game | Installs |" in md, "the by-game crash table must carry its install base"
    # The retention section is always present; the fixture is too short to draw
    # the curve, so it says so rather than vanishing.
    assert "## Retention" in md and ("still launching a month later" in md
                                     or "Too few new installs" in md), \
        "the retention section must state its figures or say why not"
    # The survival curve is GONE, not merely unreferenced (Report.__init__ sweeps
    # stale SVGs): it could only fall and read as decline; the section leads
    # with the two figures as text and ends on the launches-per-install histogram.
    assert not os.path.exists(os.path.join(out, "charts", "retention.svg")), \
        "retention.svg is back - the falling curve reads as decline; state the figures"

    # The cohort and rate helpers, on frames small enough to check by hand -- the
    # fixture spans two days, so the charts they feed are legitimately not drawn.
    d0 = pd.Timestamp("2026-01-05").date()   # a Monday
    span = pd.DataFrame({"install_id": ["a", "a", "b", "b", "c"],
                         "date": [d0, d0 + timedelta(days=40), d0, d0 + timedelta(days=3), d0 + timedelta(days=1)]})
    ax = {"observed": [d0 + timedelta(days=i) for i in range(60)]}
    curve, n, newest, ncurve, nn = retention_curve(span, ax, min_size=1)
    assert n == 3 and [round(curve[N]) for N in (0, 1, 7, 30)] == [100, 67, 33, 33], \
        "retention must be survival at N+ days: a and b came back after a day, only a after 7 and 30"
    assert newest == d0 and nn == 3 and round(ncurve[7]) == 33, "the one cohort is the newest"
    _c, n_since, *_ = retention_curve(span, ax, since=d0 + timedelta(days=1), min_size=1)
    assert n_since == 1, "since= drops the installs first seen before it"
    short_ax = {"observed": [d0 + timedelta(days=i) for i in range(20)]}
    curve_short, n_short, *_ = retention_curve(span, short_ax, min_size=1)
    # Twenty observed days: nobody has had 30 days to come back yet.
    assert curve_short == [] and n_short == 0, \
        "an install the window cannot yet measure must be left out, not read as churn"
    # One row per achievement somebody HOLDS - the fixture's Racer and Tinkerer, and
    # not the 80-odd listed rows nobody has, which were empty bars saying only that the
    # catalogue is new. The count line under the chart carries that instead.
    assert glob_svg.count('clip-path="url(#b') == 2, \
        "chart should carry one row per held achievement, got {}".format(
            glob_svg.count('clip-path="url(#b'))
    assert "**Earned so far:** 2 of the" in md, \
        "the section must say how much of the catalogue is charted"
    # The bar is SPLIT BY TIER, not a flat holder count: the fixture holds Racer at
    # tier 2 and Tinkerer at tier 1, so both classes have to appear. Reading the tier
    # off the ping's VALUE is the whole point -- an achievement most people have and
    # one most people have finished are the same bar without it.
    assert 'class="t2"' in glob_svg and 'class="t1"' in glob_svg, \
        "achievement bars must be split into tier segments"
    assert ">Platinum<" in glob_svg, "tier chart needs its legend - colour alone is not identity"
    # Icons are INLINED. A committed SVG that merely references ../assets/icons/x.svg
    # renders as an empty row once GitHub serves the chart as an image.
    assert 'class="ico"' in glob_svg and "assets/icons" not in glob_svg, \
        "achievement rows should carry inlined icon paths, not references"
    # EVERY BULLET UNDER THE HEADING IT BELONGS TO. The achievements block first
    # shipped inside _engagement, so its heading was emitted mid-section and the
    # session-length bullets that follow rendered underneath it -- a median session
    # length filed as an achievement figure. Nothing else here would have caught it:
    # both sections were present, both charts were written, every number was right.
    def section_of(needle):
        head = None
        for line in md.split("\n"):
            if line.startswith("## "):
                head = line[3:].strip()
            elif needle in line:
                return head
        return None

    assert section_of("**Median session:**") == "Activity over time", \
        "session length belongs to Activity over time, not " + str(section_of("**Median session:**"))
    assert "## Repeat usage" not in md, \
        "Repeat usage is back - it promised retention and delivered a histogram"
    launch_svg = open(os.path.join(out, "charts", "launch_counts.svg"), encoding="utf-8").read()
    assert ">101–500<" in launch_svg and ">500+<" in launch_svg, "the launch histogram lost its 101–500 / 500+ split"
    assert section_of("launch_counts.svg") == "Retention", \
        "launches per install belongs under Retention, not " + str(section_of("launch_counts.svg"))
    assert section_of("**Achievements unlocked") == "Achievements", \
        "achievement totals belong to the Achievements section"
    # The 1.30.0 install reports ach_pct=0 because its ping was built before the save
    # file loaded. Counted, it would drag every achievement figure toward zero and put a
    # player who has earned plenty in the "0%" bucket, so the section states the
    # exclusion and divides by the 1.30.1+ installs only.
    assert "**installs holding at least one achievement:** 100% (1 of 1)" in md, \
        "1.30.0's false zero must be out of the achievement denominator"
    # The 1.30.0 install is silently out of the denominator now: the report no longer
    # narrates the exclusion (the build it describes is nearly gone), so the figures
    # themselves are the only place it can be caught.
    assert "hard zero" not in md, "the 1.30.0 exclusion note should no longer be printed"

    # The intensity curve, both halves. Producing FEWER charts is not an error
    # unless something asserts otherwise, so a section that silently stopped
    # drawing it would still print `selftest OK` -- and it is the only chart in the
    # report whose finding no other chart can show (the daily-launches curve it
    # replaced ran near-parallel to active installs, so a drift in the RATIO was
    # invisible in both).
    assert os.path.exists(os.path.join(out, "charts", "activity_launches_per_active.svg")), \
        "launches-per-active chart missing - the section can no longer show " \
        "engagement moving, only the population and a window average"
    assert "activity_launches_per_active.svg" in md, \
        "intensity chart produced but REPORT.md does not reference it"
    # The daily-launches chart is GONE, not merely unreferenced: it was active
    # installs times a near-constant, and Report.__init__ sweeps stale SVGs, so a
    # reintroduction would be a deliberate act rather than a leftover file.
    assert not os.path.exists(os.path.join(out, "charts", "activity_launches.svg")), \
        "activity_launches.svg is back - it duplicated the active-installs curve"

    # A RATIO, not a count: every plotted day has at least as many launches as
    # active installs, because an install is only active on a day it launched. A
    # series that reverted to counting rows, or divided the wrong way round, lands
    # under 1.0 and nothing else in this file would notice.
    for _pt in re.findall(r'points="([^"]+)"', open(
            os.path.join(out, "charts", "activity_launches_per_active.svg")).read()):
        assert _pt.strip(), "empty polyline in the intensity chart"


    # SECTION ORDER (see build()): the players' sections first, the developer's
    # after -- method last. Pinned because a reordering reads as a normal edit;
    # both the order and Geography after the usage sections are the author's call.
    _secs = [md.index("## " + h) for h in
             ("Games", "Activity over time", "Retention",
              "Geography", "Achievements", "Feature & HUD adoption",
              "Operating system", "Plugin version adoption", "Crashes",
              "About this data")]
    assert _secs == sorted(_secs), "the report's section order changed"

    # THE CONTAINER RULE (breakdown() has it in full): a breakdown is a TABLE,
    # not a two-bar chart and not a sentence. Pinned on the two that were charts
    # and the one that was prose, since each would revert as an ordinary edit.
    assert "| OS | Installs |" in md, \
        "the OS breakdown is a table, not a chart"
    assert "| Access-violation type | Reports |" in md, \
        "the access-violation breakdown is a table, not a prose list"
    for _gone in ("os.svg", "runtime_steam.svg"):
        assert not os.path.exists(os.path.join(out, "charts", _gone)), \
            _gone + " is back - a two- or three-category breakdown is a table"
    # A breakdown's rows carry count AND share; a bare count cannot be compared
    # against the rest of the report's figures.
    assert re.search(r"\| Windows 11 \| [<>]?\d+% \([\d,]+\) \|", md), \
        "a breakdown row states its count and its share"

    # RULE 3, the one shape for a chart's numbers: figures ABOVE the chart they
    # summarise (Report.figures has the why). Asserted on the section that had it
    # the other way round, since a revert there reads as a normal edit.
    # ORDER within Activity: population, then intensity, then the lifetime total.
    # The section used to open on an EVENT count, the one number that reads as an
    # audience when it is not -- and every bullet now sits under the chart it
    # describes (avg launches/day used to sit two charts below its own).
    _order = [md.index("charts/activity_cumulative_installs.svg"),
              md.index("charts/activity_new_vs_returning.svg"),
              md.index("charts/activity_launches_per_active.svg"),
              md.index("charts/activity_heatmap.svg"),
              md.index("charts/session_length.svg")]
    assert _order == sorted(_order), \
        "Activity must read unique installs -> new-versus-returning -> intensity -> " \
        "when people ride -> session length (the one chart with no time axis, last)"
    # The launch scalars sit with the chart they summarise wherever that chart
    # moves -- between the population curve and the intensity one, not stranded
    # at the end of the section as avg launches/day once was.
    assert md.index("charts/activity_new_vs_returning.svg") \
        < md.index("**avg launches/day:**") \
        < md.index("charts/activity_launches_per_active.svg"), \
        "the launch scalars belong immediately above the intensity chart"
    # THE TWO CUT CHARTS STAY CUT (Report.__init__ sweeps stale SVGs, so a file
    # here is a reintroduction): they plotted the daily total and its new subset,
    # which new-versus-returning carries whole. The running total is NOT one of
    # them any more: it shows a level no other chart shows (see _activity).
    for _gone in ("activity_active_installs.svg", "activity_new_installs.svg"):
        assert not os.path.exists(os.path.join(out, "charts", _gone)), \
            _gone + " is back - it plotted a quantity new-versus-returning already carries"
    # One scale per section: the Activity line charts are all linear.
    for _lin in ("activity_cumulative_installs.svg", "activity_new_vs_returning.svg",
                 "activity_launches_per_active.svg"):
        assert "log scale" not in open(os.path.join(out, "charts", _lin)).read(), \
            _lin + " is on a log axis beside linear neighbours"

    # The size figure, in all three places, AGREEING. avg_active_per_day exists
    # because the trimmed denominator is the half that drifts when it is written
    # out twice, and a tile quietly disagreeing with the bullet under the chart
    # is precisely the failure nobody would catch by reading either one.
    _tiles = re.findall(r"^\| ([^|*]+?) \| \*\*", md[md.find("| At a glance |"):], re.M)[:6]
    assert _tiles == ["Unique installs", "Downloads", "Releases since Dec 2025", "Avg active/day",
                      "Launches", "Countries"], \
        "the header tiles must carry the average active/day beside the lifetime Installs total: {}".format(_tiles)
    assert "| Releases since Dec 2025 | **4** |" in md, "releases tile wrong"

    # Downloads: summed per release line, ordered by VERSION (1.10 above 1.9, which a
    # string sort gets wrong), with the total in the table and the header tile alike.
    # One table: installs and downloads side by side, a missing number left blank.
    tbl = md[md.find("| Release line | Released | Unique installs | Downloads |"):]
    assert re.search(r"\| 1\.30 \| 2026-09-06 \| [^|]+ \| 120 \|\n(?:\| 1\.2\d \|  \| [^|]+ \|  \|\n)+"
                     r"\| 1\.10 \| 2025-12-29 \|  \| 7 \|\n\| 1\.9 \| 2025-12-23 \|  \| 5 \|\n", tbl), \
        "downloads rows wrong:\n" + tbl[:600]
    assert re.search(r"^\| \*\*Total\*\* \|  \| \*\*[\d,]+\*\* \| \*\*132\*\* \|$", tbl, re.M), \
        "total row wrong:\n" + tbl[:600]
    assert "| Downloads | **132** |" in md, \
        "the Downloads tile must carry the same total as the table"

    # The README's badge reads badge.json, so it must carry the SAME lifetime total
    # as the Installs tile -- a badge that disagrees with the page it links to is
    # the one way this could go wrong unnoticed.
    badge = json.load(open(os.path.join(out, "badge.json")))
    tile_installs = int(re.search(r"\| Unique installs \| \*\*([\d,]+)\*\* \|", md).group(1).replace(",", ""))
    assert badge == {"schemaVersion": 1, "label": "unique installs",
                     "message": compact_count(tile_installs), "color": "blue"}, badge
    assert [compact_count(n) for n in (7, 999, 1000, 14782, 99960, 148200, 1500000)] == \
        ["7", "999", "1k", "14.8k", "100k", "148k", "1.5M"]

    # The fixture's two installs launch on the same days, so a per-day DISTINCT
    # count must stay under the launch count -- the whole point of the chart. A
    # series that quietly reverted to counting rows would pass every assertion
    # above and read as a much larger audience.
    assert "**avg launches per active install:**" in md, \
        "the intensity bullet must state launches per active install"
    _lpa = float(re.search(r"\*\*avg launches per active install:\*\* ([\d.,]+)", md)
                 .group(1).replace(",", ""))
    assert _lpa > 1.0, \
        "launches per active install came out at {} - a distinct count cannot " \
        "exceed the launches it is drawn from, so the series is counting rows".format(_lpa)

    # The label props (theme, spotter) count as features: their on/off half is
    # DERIVED from the label (no feat_ for it is on the wire, so
    # a features loop that only read sent flags would silently drop it). Asserted
    # on the derivation itself: this fixture sits under MIN_ADOPTION_BASE and
    # draws no features chart at all.
    probe = pd.DataFrame({"_n": [{"feat_steam": 1}, {"feat_steam": 1}, {"feat_steam": 0}],
                          "_s": [{"panel_theme": "carbon-dark", "spotter": "default"},
                                 {"panel_theme": "none", "spotter": "none"}, {}]})
    derived = flag_adoption(derive_label_flags(probe), "feat_")
    for _, flag in LABEL_PROPS:
        assert (flag, 1, 2) in derived, \
            flag + " must read on for a pack, off for none, and absent for no label: " + str(derived)
        assert flag not in probe["_n"][0], "derive_label_flags must not mutate its input"

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
    # The line's END value is written at its end (its peak IS its end here, so no
    # peak label); a series that peaks mid-way gets the dot and the value too.
    assert "7,637</text>" in log_svg, "the end value is not labelled on a log chart"
    assert "<circle" not in log_svg, "a peak that is the end must not get a second label"
    peak_svg = svg.lines("t", ["a", "b", "c", "d"], [("s", [5, 9, 2, 1], "#ffffff")])
    assert "<circle" in peak_svg and ">9</text>" in peak_svg, "a mid-series peak must be marked and valued"
    assert ">1</text>" in peak_svg, "the end value must be labelled on a linear chart"
    # Two series ending together must not print through each other: their labels are
    # nudged at least a text row apart.
    pair_svg = svg.lines("t", ["a", "b"], [("p", [1, 1000], "#ffffff"), ("q", [1, 1001], "#000000")], log=True)
    label_ys = sorted(float(m) for m in re.findall(r'<text x="[0-9.]+" y="([0-9.]+)" style="fill:#(?:ffffff|000000)', pair_svg))
    assert len(label_ys) == 2 and label_ys[1] - label_ys[0] >= 12, "end labels overprint: {}".format(label_ys)
    # Each new-install scalar sits on the chart it is the slope or the band of.
    assert "**avg new installs/day:**" in md and \
           md.index("**avg new installs/day:**") < md.index("charts/activity_cumulative_installs.svg"), \
        "avg new installs/day belongs above the running total it is the slope of"
    assert md.index("charts/activity_cumulative_installs.svg") \
           < md.index("**share of a day's active installs that are new:**") \
           < md.index("charts/activity_new_vs_returning.svg"), \
        "the new share belongs above the new-versus-returning chart"
    # annotate="legend": today's value rides in the key ("v2 · 40%"), not down the
    # right margin, where ten same-coloured numbers stacked into an unmatchable column.
    leg_svg = svg.lines("t", ["a", "b", "c"], [("v1", [90, 70, 60], "#ffffff"), ("v2", [10, 30, 40], "#000000")],
                        annotate="legend")
    assert "v1 · 60</text>" in leg_svg and "v2 · 40</text>" in leg_svg, \
        "legend mode must put each series' last value in its key"
    assert not re.search(r'style="fill:#(?:ffffff|000000);font-size:11px', leg_svg), \
        "legend mode must not also write the end values down the right margin"
    assert "<circle" in leg_svg and ">90</text>" in leg_svg, "legend mode must keep the peak labels"
    # A bar-end value is drawn inside the frame whatever its length: the margin is
    # sized from the widest one ("100% (2,868 of 2" was cut off at a fixed 96px).
    long_lbl = "100% (2,868 of 2,868)"
    bar_svg = svg.hbar("t", [("a", 100, None, long_lbl), ("b", 5, None, "5%")])
    m = re.search(r'<text x="([0-9.]+)" y="[0-9.]+" class="val">' + re.escape(long_lbl), bar_svg)
    assert m and float(m.group(1)) + 7.4 * len(long_lbl) <= 760, "a bar-end value runs past the chart edge"

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
    assert os.path.exists(build(cdf, csv_out)[0])

    # ---- Rollup round trip --------------------------------------------------
    # THE CONTRACT the rollup lives or dies by: a stretch of data that has aged into
    # usage_survey/rollup/ must produce the same report as the raw export it was made
    # from. Asserted by splitting the fixture at its day boundary and comparing
    #   day 1 rolled up + day 2 raw   against   both days raw.
    # Comparing the whole REPORT.md rather than a handful of figures is deliberate:
    # the digest drops most of what an export carries, and the interesting failure is
    # a metric nobody thought to re-check quietly reading 0 (the coverage table did
    # exactly that on the first draft, because a stored launch has no props).
    day1 = [x for x in rows if x["timestamp"] < base + 86400]
    day2 = [x for x in rows if x["timestamp"] >= base + 86400]
    assert day1 and day2, "fixture must straddle a day boundary for this to test anything"
    roll_dir = tempfile.mkdtemp(prefix="analytics_selftest_rollup_")
    seed_out = tempfile.mkdtemp(prefix="analytics_selftest_seed_")
    seed = mkdf(day1)
    _, seed_snap = build(seed, seed_out)
    rollup.write(roll_dir, seed, seed_snap)

    hist, hist_snap = rollup.read(roll_dir)
    assert hist is not None and len(hist) == len(day1), \
        "rollup must restore one row per stored event, got {}".format(len(hist))
    assert set(hist["event_name"]) == set(seed["event_name"]), "rollup lost an event kind"

    merged_out = tempfile.mkdtemp(prefix="analytics_selftest_merged_")
    merged_path, _ = build(rollup.merge_events(hist, mkdf(day2)), merged_out,
                           snap_hist=hist_snap)
    raw_out = tempfile.mkdtemp(prefix="analytics_selftest_raw_")
    raw_path, _ = build(mkdf(rows), raw_out)
    for name in sorted(os.listdir(os.path.join(raw_out, "charts"))):
        a = open(os.path.join(raw_out, "charts", name), "rb").read()
        b_path = os.path.join(merged_out, "charts", name)
        assert os.path.exists(b_path), "rollup report is missing chart " + name
        assert open(b_path, "rb").read() == a, \
            "chart {} differs between the raw and rolled-up runs".format(name)
    if open(merged_path).read() != open(raw_path).read():
        import difflib
        diff = "\n".join(list(difflib.unified_diff(
            open(raw_path).read().split("\n"), open(merged_path).read().split("\n"),
            "raw", "rollup", lineterm=""))[:40])
        raise AssertionError("rollup report differs from the raw report:\n" + diff)

    # A re-run must not double-count: re-reading an export whose days the rollup
    # already holds replaces those days rather than adding to them. This is the whole
    # reason merge_events() drops by DAY instead of de-duplicating rows -- the digest
    # has no event identity left to de-duplicate on.
    again = rollup.merge_events(hist, mkdf(day1))
    assert len(again) == len(day1), \
        "re-reading a stored day must replace it, not append ({} rows)".format(len(again))

    # And the files are byte-stable, so a month nobody touched shows no git diff.
    before = open(os.path.join(roll_dir, "installs.csv.gz"), "rb").read()
    rollup.write(roll_dir, seed, seed_snap)
    assert open(os.path.join(roll_dir, "installs.csv.gz"), "rb").read() == before, \
        "rewriting unchanged rollup data must be byte-identical"

    print("selftest OK -> {}".format(path))


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser(
        description="Generate a static Markdown+SVG analytics dashboard from Aptabase exports (CSV or Parquet).")
    ap.add_argument("inputs", nargs="*",
                    help="Export file(s) or globs (.csv or .parquet). May be omitted to "
                         "re-render from the stored rollup alone.")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "usage_survey"),
                    help="output directory (default: usage_survey/)")
    ap.add_argument("--no-rollup-write", action="store_true",
                    help="read the stored rollup but do NOT write it back. The daily "
                         "usage-survey job runs this way so only REPORT.md and the "
                         "charts change: the rollup's gzip shards are rewritten "
                         "wholesale and git cannot delta them, so committing them "
                         "daily would dominate the repository's history. A separate "
                         "monthly run checkpoints the rollup forward.")
    ap.add_argument("--no-rollup", action="store_true",
                    help="ignore the stored rollup and do not update it - the report then "
                         "covers only the exports given. To REBUILD the rollup instead, "
                         "delete <out>/rollup and re-run with every export.")
    args = ap.parse_args()

    paths = []
    for pat in args.inputs:
        hit = sorted(glob.glob(pat))
        paths.extend(hit if hit else [pat])
    missing = [p for p in paths if not os.path.exists(p)]
    for p in missing:
        print("warning: no such export: {}".format(p), file=sys.stderr)
    paths = [p for p in paths if os.path.exists(p)]

    rollup_dir = os.path.join(args.out, "rollup")
    hist, hist_installs = (None, None) if args.no_rollup else rollup.read(rollup_dir)
    if hist is not None:
        print("Rollup: {:,} stored events, {} → {}".format(
            len(hist), hist["date"].min(), hist["date"].max()))

    fresh = None
    if paths:
        print("Reading {} file(s)...".format(len(paths)))
        fresh = load(paths)
        print("  {:,} events, {} → {}".format(
            len(fresh), fresh["date"].min(), fresh["date"].max()))
    elif hist is None:
        sys.exit("error: no exports found and no rollup in {}".format(rollup_dir))

    df = rollup.merge_events(hist, fresh)
    print("Reporting on {:,} events, {} → {}".format(
        len(df), df["date"].min(), df["date"].max()))
    out, snap = build(df, args.out, snap_hist=hist_installs)
    print("Wrote {}".format(out))
    print("Charts in {}".format(os.path.join(args.out, "charts")))
    if args.no_rollup_write:
        print("Rollup NOT updated (--no-rollup-write); still covers through {}.".format(
            hist["date"].max() if hist is not None else "nothing"))
    elif not args.no_rollup:
        manifest = rollup.write(rollup_dir, df, snap)
        print("Rollup updated: {} month(s), {:,} installs in {}".format(
            len(manifest["months"]), manifest["installs"], rollup_dir))


if __name__ == "__main__":
    main()
