#!/usr/bin/env python3
"""
tools/director_report.py — broadcast analysis of the auto-director from a plugin log.

The auto-director writes one line per camera cut into the NORMAL session log
(`<savePath>\\mxbmrp3\\mxbmrp3_log.txt`, produced by every build incl. release):

    [12:34:56.789] [INFO] Director cut: t=12345ms #7 shot=battle cam=Trackside partner=9 reason=story

This reads those lines from one or more logs and reports what the broadcast looked
like — total cuts, cut rate, shot-length spread, the shot-type and camera mix
(share of airtime), and per-rider screen time. It's the same report the headless
`director_broadcast_test` produces from tape replays, but pointed at a REAL in-game
replay/spectate session's log, so you can review the direction of any session.

Usage:
    python3 tools/director_report.py mxbmrp3_log.txt
    python3 tools/director_report.py --min 8 --max 25 sessionA.log sessionB.log

How it reads the timeline:
  * `t=<ms>` is the plugin's own monotonic clock — used for shot lengths (robust,
    no midnight-rollover, always increasing within a launch).
  * The last shot is still open when the log ends, so it's bounded by the log's
    final timestamp (the wall-clock delta from the last cut, added onto its `t`).
  * A single very long "longest shot" usually just means the session sat in menus
    (the director only cuts on live data, so no cuts flow while paused) — eyeball it
    rather than reading it as real airtime. `--gap` caps any one shot's counted
    length so a menu pause doesn't dominate the airtime shares.

Pure stdlib; no dependencies.
"""

import argparse
import re
import sys
from collections import defaultdict

TS_RE = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\.(\d\d\d)\]")
CUT_RE = re.compile(
    r"Director cut: t=(\d+)ms #(-?\d+) shot=(\w+) cam=(.+?) partner=(-?\d+)"
    r"(?: reason=(\S+))?"  # reason is appended (older logs won't have it -> "?")
)

# Pacing reasons whose cut is ALLOWED to come in under the min-shot floor by design:
# a crash interrupt (breaking news), the finish snap, the first shot / a subject that
# left / a session reset (all "nothing to hold"). Any OTHER reason ending a sub-min
# shot is a real min-shot violation.
MINSHOT_BYPASS = {"acquire", "subject-gone", "incident", "finish"}


def parse_log(path):
    """Return (cuts, session_end_wall_ms). cuts: list of dicts with t/num/shot/cam/wall."""
    cuts = []
    session_end = None
    day = 0
    prev_raw = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TS_RE.match(line)
            if not m:
                continue
            hh, mm, ss, ms = (int(m.group(i)) for i in range(1, 5))
            raw = ((hh * 60 + mm) * 60 + ss) * 1000 + ms
            # The wall clock is HH:MM:SS.mmm with no date, so add a day each time it
            # steps backward (a session that crossed midnight).
            if prev_raw is not None and raw < prev_raw - 1000:
                day += 1
            prev_raw = raw
            wall = day * 86_400_000 + raw
            session_end = wall
            c = CUT_RE.search(line)
            if c:
                cuts.append(
                    {
                        "t": int(c.group(1)),
                        "num": int(c.group(2)),
                        "shot": c.group(3),
                        "cam": c.group(4),
                        "reason": c.group(6) or "?",
                        "wall": wall,
                    }
                )
    return cuts, session_end


def median(vals):
    s = sorted(vals)
    return s[len(s) // 2] if s else 0.0


def gini(vals):
    """Gini coefficient of non-negative values (airtime per rider). 0.0 = every shown
    rider got equal airtime; ->1.0 = one rider hogged it all. A compact fairness score
    for comparing how evenly the field was covered across configs/sessions."""
    xs = sorted(v for v in vals if v > 0)
    n = len(xs)
    total = sum(xs)
    if n == 0 or total == 0:
        return 0.0
    # Gini via the sorted-rank formula, i 1-based over the ascending values.
    weighted = sum((i + 1) * x for i, x in enumerate(xs))
    return (2.0 * weighted) / (n * total) - (n + 1.0) / n


def report(cuts, session_end, label, mn, mid, mx, gap):
    print(f"\n================ Broadcast analysis: {label} ================")
    n = len(cuts)
    if n == 0:
        print("  (no director cuts in this log — was the auto-director on and spectating?)")
        print("=" * 56)
        return
    if n == 1:
        print(f"  1 cut only: #{cuts[0]['num']} ({cuts[0]['shot']}, {cuts[0]['cam']}) — too short to profile")
        print("=" * 56)
        return

    # Bound the last (still-open) shot with the log's final timestamp: how much wall
    # time passed after the last cut, added onto its monotonic `t`.
    last = cuts[-1]
    tail_ms = max(0, session_end - last["wall"]) if session_end is not None else 0
    end_t = last["t"] + tail_ms
    dur_s = max(0.0, (end_t - cuts[0]["t"]) / 1000.0)

    lens = []
    shot_air = defaultdict(float)
    cam_air = defaultdict(float)
    rider_air = defaultdict(float)
    reason_ct = defaultdict(int)
    below, atmin, midb, atmax = 0, 0, 0, 0
    capped = 0
    short_bypass, short_violation, violation_samples = 0, 0, []
    for i in range(n):
        end = cuts[i + 1]["t"] if i + 1 < n else end_t
        length = max(0.0, (end - cuts[i]["t"]) / 1000.0)
        if gap and length > gap:
            length = gap  # a menu pause — don't let one idle shot dominate the shares
            capped += 1
        lens.append(length)
        shot_air[cuts[i]["shot"]] += length
        cam_air[cuts[i]["cam"]] += length
        rider_air[cuts[i]["num"]] += length
        reason_ct[cuts[i].get("reason", "?")] += 1
        if length < mn:
            below += 1
            # A shot under the floor is legitimate only if the cut that ENDED it (the
            # next cut) was a by-design bypass; otherwise it's a real min-shot violation.
            if i + 1 < n:
                r = cuts[i + 1].get("reason", "?")
                if r in MINSHOT_BYPASS or r == "?":
                    short_bypass += 1
                else:
                    short_violation += 1
                    if len(violation_samples) < 4:
                        violation_samples.append(
                            f"{cuts[i]['shot']}->{cuts[i + 1]['shot']} {length:.1f}s (reason={r})"
                        )
        elif length < mid:
            atmin += 1
        elif length <= mx + 0.5:
            midb += 1
        else:
            atmax += 1

    air_total = sum(lens) or 1.0
    print(f"  Session length:  {dur_s:.1f} s  ({dur_s / 60.0:.1f} min)")
    print(f"  Total cuts:      {n}")
    print(
        f"  Cut rate:        {n / (dur_s / 60.0):.1f} cuts/min   "
        f"(avg shot {dur_s / n:.1f} s, median {median(lens):.1f} s, longest {max(lens):.1f} s)"
    )
    print(
        f"  Shot length mix: <{mn}s {100.0 * below / n:.0f}%  |  {mn}-{mid}s {100.0 * atmin / n:.0f}%  "
        f"|  {mid}-{mx}s {100.0 * midb / n:.0f}%  |  >{mx}s {100.0 * atmax / n:.0f}%"
    )
    if capped:
        print(f"  (capped {capped} shot(s) at --gap {gap:.0f}s — likely menu pauses, not airtime)")

    # Min-shot compliance: of the shots that came in under the floor, how many did so via
    # a by-design bypass vs a genuine violation (a shot cut short by a pacing cut that is
    # supposed to honor the floor). A clean director shows 0 violations.
    classified = short_bypass + short_violation  # excludes the final open shot (no end cut)
    if classified:
        flag = "OK" if short_violation == 0 else f"!! {short_violation} VIOLATION(S)"
        print(
            f"  Min-shot ({mn:.0f}s):   {classified} sub-min shot(s): "
            f"{short_bypass} by-design bypass, {short_violation} violation  [{flag}]"
        )
        if violation_samples:
            print(f"      violations: {', '.join(violation_samples)}")

    def print_share(m, title):
        print(f"  {title}")
        for k, v in sorted(m.items(), key=lambda kv: kv[1], reverse=True):
            print(f"      {k:<14} {100.0 * v / air_total:5.1f}%  ({v:.0f} s)")

    print_share(shot_air, "Shot types (share of airtime):")
    print_share(cam_air, "Cameras (share of airtime):")

    # Cut reasons: WHY the camera cut, tallied over cuts (not airtime). "?" means the log
    # predates the reason field. This is what distinguishes otherwise identical solo cuts
    # (a max-shot dip vs an acquire vs a subject-gone).
    if any(r != "?" for r in reason_ct):
        print("  Cut reasons (share of cuts):")
        for k, c in sorted(reason_ct.items(), key=lambda kv: kv[1], reverse=True):
            print(f"      {k:<14} {100.0 * c / n:5.1f}%  ({c})")

    riders = sorted(rider_air.items(), key=lambda kv: kv[1], reverse=True)
    print(f"  Rider airtime:   {len(riders)} distinct riders got screen time")
    top_n, top_s = riders[0]
    bot_n, bot_s = riders[-1]
    print(f"      most:   #{top_n}  {top_s:.1f} s  ({100.0 * top_s / air_total:.1f}%)")
    print(f"      least:  #{bot_n}  {bot_s:.1f} s  ({100.0 * bot_s / air_total:.1f}%)")
    print(f"      median per shown rider: {median([s for _, s in riders]):.1f} s")
    # Fairness: how evenly airtime was split among shown riders. The top-rider share is
    # the intuitive "is it glued to one rider?" number; Gini summarizes the whole spread
    # (0 = perfectly even, higher = more lopsided). A pure lull sits high (leader-anchored);
    # a story-rich broadcast spreads lower.
    top3 = sum(s for _, s in riders[:3])
    print(
        f"      fairness: top rider {100.0 * top_s / air_total:.0f}%  |  "
        f"top-3 {100.0 * top3 / air_total:.0f}%  |  Gini {gini([s for _, s in riders]):.2f}"
    )
    print("=" * 56)


def main():
    ap = argparse.ArgumentParser(
        description="Broadcast analysis of the auto-director from plugin log file(s).",
        epilog="See the module docstring for details.",
    )
    ap.add_argument("logs", nargs="+", help="plugin log file(s) (mxbmrp3_log.txt)")
    ap.add_argument("--min", type=float, default=8, dest="mn",
                    help="min-shot bucket edge, s (default 8 = director default min shot)")
    ap.add_argument("--mid", type=float, default=15, help="middle bucket edge, s (default 15)")
    ap.add_argument("--max", type=float, default=25, dest="mx",
                    help="max-shot bucket edge, s (default 25 = director default max shot)")
    ap.add_argument("--gap", type=float, default=0,
                    help="cap any single shot at this many s when tallying airtime "
                         "(0 = off; use e.g. 60 to discount menu pauses)")
    args = ap.parse_args()

    rc = 0
    for path in args.logs:
        try:
            cuts, session_end = parse_log(path)
        except OSError as e:
            print(f"error: cannot read {path}: {e}", file=sys.stderr)
            rc = 1
            continue
        report(cuts, session_end, path, args.mn, args.mid, args.mx, args.gap)
    return rc


if __name__ == "__main__":
    sys.exit(main())
