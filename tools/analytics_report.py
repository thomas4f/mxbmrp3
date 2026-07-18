#!/usr/bin/env python3
# ============================================================================
# tools/analytics_report.py
# Turn Aptabase monthly Parquet exports into a static Markdown dashboard + SVG
# charts, checked into analytics/ (the raw exports are NOT kept in the repo).
#
#   python3 tools/analytics_report.py <export1.parquet> [<export2.parquet> ...]
#   python3 tools/analytics_report.py path/to/exports/*.parquet --out analytics
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
# Dependencies: pyarrow (parquet), pandas. Dev-only -- see tools/requirements.txt.
# Sibling module tools/analytics_svg.py holds the (dependency-free) SVG charts.
# ============================================================================
import argparse
import glob
import json
import os
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
# and deliberately trigger crashes to validate the telemetry pipeline (e.g. the
# forced null-write in handleDraw used to test 1.27.5's new stack capture), which
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


def load(paths):
    frames = []
    for p in paths:
        try:
            frames.append(pd.read_parquet(p))
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
    df["ts"] = pd.to_datetime(df["timestamp"], unit="s", utc=True)
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
    games_seen = sorted(started["game"].unique())

    # ---- Header + summary tiles ------------------------------------------
    r.w("# MXBMRP3 - Analytics Report", "")
    r.w("<!-- GENERATED by tools/analytics_report.py from Aptabase exports. "
        "Do not edit by hand; re-run the tool. Raw exports are not kept in the repo. -->", "")
    r.w("**Data window:** {} → {} ({} days)".format(d0, d1, n_days), "")
    tiles = [
        ("Installs", "{:,}".format(installs)),
        ("Launches", "{:,}".format(len(started))),
        ("Games", str(len(games_seen))),
        ("Countries", str(snap["country_name"].replace("", pd.NA).nunique())),
        ("Crash reports", "{:,}".format(len(crashes))),
    ]
    r.w("| " + " | ".join(t[0] for t in tiles) + " |")
    r.w("|" + "|".join(["---"] * len(tiles)) + "|")
    r.w("| " + " | ".join("**{}**".format(t[1]) for t in tiles) + " |", "")

    _highlights(r, df, started, sessions, crashes, snap)
    _activity(r, df, started, sessions)
    _games(r, started, snap)
    _versions(r, snap)
    _geography(r, snap)
    _os(r, snap)
    _engagement(r, sessions, snap)
    _features(r, snap)
    _crashes(r, started, sessions, crashes)
    _coverage(r, started, snap, dev_installs, dev_events)

    r.w("---")
    r.w("*Generated by `tools/analytics_report.py`. Charts in `charts/`. "
        "Re-run after each monthly Aptabase export; the raw `.parquet` files stay out of the repo.*")
    return r.save()


def _highlights(r, df, started, sessions, crashes, snap):
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

    # stability headline
    cr, stmin, _ = crash_population(started, crashes)
    if len(stmin):
        lookup = load_known_crashes()[1]
        cr = cr.copy()
        cr["known"] = cr.apply(lambda row: match_known(row["fault"], row["game_build"], lookup), axis=1)
        names = cr["known"].dropna().map(lambda k: k["name"])
        top_crash = names.value_counts().index[0] if len(names) else None
        line = "**Stability:** {} of {}+ launches produced a crash report; where a location " \
               "was recorded, the fault was outside the plugin (see the crash breakdown)." \
               .format(pctstr(len(cr), len(stmin)), CRASH_MIN_STR)
        if top_crash:
            line += " Most common: *{}*.".format(top_crash)
        bullets.append(line)

    r.w("## Highlights", "")
    for b in bullets:
        r.w("- " + b)
    r.w("")


def _activity(r, df, started, sessions):
    r.w("## Activity over time", "")
    days = sorted(df["date"].unique())
    xlabels = [d.strftime("%m-%d") for d in days]

    def game_series(game_list):
        out = []
        for i, g in enumerate(game_list):
            by_day = started[started["game"] == g].groupby("date").size()
            out.append((g, [int(by_day.get(d, 0)) for d in days],
                        svg.GAME_COLORS.get(g, svg.PALETTE[i % len(svg.PALETTE)])))
        return out

    # Split games by volume: one dominant game flattens the others to zero on a
    # shared axis, so low-volume games get their own chart at their own scale.
    totals = started.groupby("game").size().sort_values(ascending=False)
    major = [g for g in totals.index if totals[g] >= 0.10 * len(started)]
    minor = [g for g in totals.index if g not in major]
    r.chart("activity_launches.svg",
            svg.lines("Daily launches" + (" - " + major[0] if len(major) == 1 else " by game"),
                      xlabels, game_series(major), subtitle="launches per day"),
            "Daily launches (main game)")
    if minor:
        r.chart("activity_launches_minor.svg",
                svg.lines("Daily launches - " + " & ".join(minor), xlabels, game_series(minor),
                          subtitle="lower-volume games, own scale"),
                "Daily launches (other games)")

    dau = started.groupby("date")["install_id"].nunique()
    r.chart("activity_active_installs.svg",
            svg.lines("Daily active installs", xlabels,
                      [("Active installs", [int(dau.get(d, 0)) for d in days], svg.PALETTE[1])],
                      subtitle="distinct installs launching per day"),
            "Daily active installs")
    peak = dau.max() if len(dau) else 0
    r.w("- **Peak daily active installs:** {:,}  ·  **avg launches/day:** {:,.0f}".format(
        int(peak), len(started) / max(1, len(days))), "")


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
    # Steam vs standalone (coverage-aware on steam_runtime)
    rep = snap[snap["_n"].map(lambda n: "steam_runtime" in n)]
    if len(rep):
        steam = int(rep["_n"].map(lambda n: bool(n.get("steam_runtime"))).sum())
        r.chart("runtime_steam.svg",
                svg.stacked_bar("Steam vs. standalone",
                                [("Steam", steam, svg.PALETTE[0]),
                                 ("Standalone", len(rep) - steam, svg.PALETTE[2])],
                                subtitle="{:,} installs reporting".format(len(rep))),
                "Steam vs standalone")


MIN_VERSION_INSTALLS = 10  # versions below this are grouped as pre-release / dev builds


def _versions(r, snap):
    r.w("## Plugin version adoption", "")
    r.w("Each install is counted once, at its **most recent** launched version, so an install "
        "that upgraded (e.g. 1.26 to 1.27) counts only under the newer version - never both. "
        "Versions with fewer than {} installs (pre-release / dev builds) are grouped.".format(
            MIN_VERSION_INSTALLS), "")
    vc = snap["app_version"].value_counts()
    total = len(snap)
    main = [(v, int(c)) for v, c in vc.items() if c >= MIN_VERSION_INSTALLS]
    tail = sum(int(c) for v, c in vc.items() if c < MIN_VERSION_INSTALLS)
    n_tail = sum(1 for v, c in vc.items() if c < MIN_VERSION_INSTALLS)
    bars = [(v, c, None, cp(c, total)) for v, c in main]
    if tail:
        bars.append(("Pre-release / dev ({} builds)".format(n_tail), tail, "#57606a",
                     cp(tail, total)))
    r.chart("versions.svg",
            svg.hbar("Installs by plugin version", bars,
                     subtitle="latest version seen per install", label_w=240),
            "Installs by plugin version")
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


def _engagement(r, sessions, snap):
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

    render("Features (feat_*)", "feat_", "features.svg",
           lambda k: FEATURE_LABELS.get(k, _pretty_key(k)))
    render("HUD adoption (hud_*)", "hud_", "huds.svg", _pretty_key)
    render("Widget adoption (widget_*)", "widget_", "widgets.svg", _pretty_key)

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
    ev("app_started", base, "userA1", "install-1", npr=flags)
    ev("app_started", base + 86400, "userA2", "install-1", npr=flags)
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
    print("selftest OK -> {}".format(path))


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser(
        description="Generate a static Markdown+SVG analytics dashboard from Aptabase Parquet exports.")
    ap.add_argument("inputs", nargs="+", help="Parquet export file(s) or globs")
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
