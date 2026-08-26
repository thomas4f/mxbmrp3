#!/usr/bin/env python3
# ============================================================================
# tools/benchmark_report.py
# Analyze the in-game/headless benchmark reports the plugin writes to
# <savePath>/mxbmrp3/benchmarks/benchmark_*.txt (developerMode=1 in-game, or the
# headless bench_driver). Pure stdlib — the standalone twin of the perf drivers,
# in the same spirit as tools/mdmp_analyze.py and tools/director_report.py.
#
# It reads the stable machine-readable "BENCH key=value ..." line each report
# carries (and the CALLBACKS table for per-callback budget flags), then:
#   * attributes cost against the 480fps budget the report declares,
#   * flags the things that actually matter at high refresh — the 1% low FPS and
#     any single callback whose peak eats a big share of one frame,
#   * --compare A B diffs two reports and flags regressions,
#   * many files => a trend table (newest last).
#
# Usage:
#   python3 tools/benchmark_report.py report.txt
#   python3 tools/benchmark_report.py --compare before.txt after.txt
#   python3 tools/benchmark_report.py benchmarks/*.txt          # trend
# ============================================================================
import argparse
import re
import shlex

# The keys the BENCH line ENDS with. Their absence means the line was cut short, not
# that the plugin chose not to emit them: they are written unconditionally, last.
#
# This exists because the line was silently truncated for real. It was built with one
# snprintf into a char[512], outgrew it, and every exported report came out clipped at
# exactly 512 bytes -- losing cb_total_us, and later all four probe_* keys, with no
# error anywhere: snprintf truncates and returns, the report still parses, and every
# surviving key is correct. Five probe sweeps were run before anyone noticed the file
# could not say which experiment produced them. The emitter is segmented now so it
# cannot truncate; this is the check that says so out loud if it ever does again.
BENCH_TERMINAL_KEYS = ("cb_total_us", "probe_n", "probe_type", "probe_fs", "probe_sprite")


def parse_bench_line(text):
    """Pull the 'BENCH ...' key=value line into a dict (numbers coerced)."""
    for line in text.splitlines():
        if line.startswith("BENCH "):
            d = {}
            for tok in shlex.split(line[len("BENCH "):]):
                if "=" not in tok:
                    continue
                k, v = tok.split("=", 1)
                try:
                    d[k] = int(v)
                except ValueError:
                    try:
                        d[k] = float(v)
                    except ValueError:
                        d[k] = v
            missing = [k for k in BENCH_TERMINAL_KEYS if k not in d]
            if missing:
                raise ValueError(
                    "BENCH line is missing its terminal keys (" + ", ".join(missing) +
                    ") -- the line was TRUNCATED, so this report cannot say which run "
                    "produced it. Re-export with a build whose BENCH emitter is segmented.")
            return d
    return None


def parse_callbacks(text):
    """Parse the CALLBACKS table rows: name, total, avg, peak, calls, %bud."""
    rows = []
    in_tbl = False
    for line in text.splitlines():
        if line.startswith("=== CALLBACKS"):
            in_tbl = True
            continue
        if in_tbl:
            if line.startswith("===") or (line.strip() == "" and rows):
                break
            if line.startswith("Name") or line.startswith("---") or not line.strip():
                continue
            m = re.match(r"\s*(\S.*?)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+([\d.]+)%\s*$", line)
            if m:
                rows.append({
                    "name": m.group(1).strip(),
                    "total_us": float(m.group(2)), "avg_us": float(m.group(3)),
                    "peak_us": float(m.group(4)), "calls": int(m.group(5)),
                    "pct_bud": float(m.group(6)),
                })
    return rows


def parse_hud_footprint(text):
    """Parse the HUD RENDER FOOTPRINT table: name, quads, strings.

    Primitive counts only — per-HUD *timing* lives in the stint table, since the
    report is written on widget-hide and a last-interval duration is whichever
    ~0.25s the capture happened to stop on.
    """
    rows = []
    in_tbl = False
    for line in text.splitlines():
        if line.startswith("=== HUD RENDER FOOTPRINT"):
            in_tbl = True
            continue
        if in_tbl:
            if line.startswith("===") or (line.strip() == "" and rows):
                break
            if line.startswith("Name") or line.startswith("---") or not line.strip():
                continue
            m = re.match(r"\s*(\S.*?)\s+(\d+)\s+(\d+)\s*$", line)
            if m:
                rows.append({
                    "name": m.group(1).strip(),
                    "quads": int(m.group(2)), "strings": int(m.group(3)),
                })
    return rows


def parse_stint(text, header):
    """Parse a STINT TOTALS table (CALLBACKS or HUD REBUILDS).

    These are the whole-session tables: name, total_us, avg_us, peak_us, count.
    The per-interval CALLBACKS / HUD RENDER FOOTPRINT tables above them cover only
    the last snapshot interval (~0.25s), so a single expensive call can dominate
    them; these are what to read when asking "what did this stint cost?".

    THE HUD TABLE CARRIES TWO MORE COLUMNS than the callback one -- `Idle us/f` (what
    a HUD costs on the frames it does NOT rebuild) and `% upd` (its share of the
    whole HUD pass) -- so the trailing group is optional and the row is NOT anchored
    on `count`. It was, and adding those two columns made every HUD row stop
    matching: the analyzer reported no stint totals at all rather than failing, which
    is the exact silent-drift failure run_perf.sh's clean-parse gate exists to catch.
    A future column lands in the same optional tail instead of breaking the parse.
    """
    rows = []
    in_tbl = False
    for line in text.splitlines():
        if line.startswith(header):
            in_tbl = True
            continue
        if in_tbl:
            if line.startswith("===") or (line.strip() == "" and rows):
                break
            if line.startswith("Name") or line.startswith("---") or not line.strip():
                continue
            # Idle can legitimately go NEGATIVE (rebuilds recorded outside the
            # recordHudUpdate bracket subtract from it), and a tail that only
            # matched positive numbers silently dropped exactly those rows —
            # the failure mode this docstring warns about, one sign short.
            m = re.match(r"\s*(\S.*?)\s+(\d+)\s+([\d.]+)\s+(\d+)\s+(\d+)"
                         r"(?:\s+(-?[\d.]+)\s+([\d.]+)%)?\s*$", line)
            if m:
                row = {
                    "name": m.group(1).strip(), "total_us": int(m.group(2)),
                    "avg_us": float(m.group(3)), "peak_us": int(m.group(4)),
                    "count": int(m.group(5)),
                }
                if m.group(6) is not None:
                    row["idle_us_per_frame"] = float(m.group(6))
                    row["pct_update"] = float(m.group(7))
                rows.append(row)
    return rows


def load(path):
    with open(path, "r", errors="replace") as f:
        text = f.read()
    b = parse_bench_line(text)
    if b is None:
        raise ValueError(f"{path}: no BENCH line (old report format?)")
    return {"path": path, "bench": b, "callbacks": parse_callbacks(text),
            "huds": parse_hud_footprint(text),
            "stint_huds": parse_stint(text, "=== HUD REBUILDS (STINT TOTALS)")}


def fmt_scenario(b):
    return (f"{b.get('game','?')} / {b.get('track','?')} "
            f"{b.get('riders','?')} riders, {b.get('huds','?')} HUDs, "
            f"{'threaded' if b.get('threaded') else 'sync'}")


def analyze_one(rep):
    b, cbs = rep["bench"], rep["callbacks"]
    budget = b.get("budget_us", 2083.0)
    target = b.get("target_fps", 480)
    print(f"\n=== {rep['path']} ===")
    print(f"Scenario : {fmt_scenario(b)}")
    print(f"Target   : {target} fps  ({budget:.0f} us/frame budget)")
    # Every figure below this line comes from the ring window (frames_sampled),
    # NOT from the session total (frames) — print both so a long capture can't
    # read as if all of its frames backed the p99. Older reports predate
    # frames_sampled=; fall back rather than misreport.
    sampled = b.get("frames_sampled")
    window = f"{sampled} sampled" if sampled is not None else "sample window unknown"
    print(f"Frames   : {b.get('frames','?')} over {b.get('dur_s','?')} s  ({window})")
    print(f"FPS      : min {b.get('fps_min','?')}  avg {b.get('fps_avg','?')}  max {b.get('fps_max','?')}"
          f"   [from the {window} frames]")
    print(f"Frame us : p50 {b.get('ft_p50_us','?')}  p99 {b.get('ft_p99_us','?')}  max {b.get('ft_max_us','?')}")
    print(f"1% low   : {b.get('lowfps_1pct','?')} fps")
    print(f"Handoff  : {b.get('quads','?')} quads ({b.get('quads_peak','?')} peak), "
          f"{b.get('strings','?')} strings ({b.get('strings_peak','?')} peak)")
    # DRAW, ATTRIBUTED -- the three per-frame timers that together account for the
    # Draw callback. Older reports carry only collect_us, so the other two default to
    # 0 and simply print as such rather than breaking the parse.
    collect = b.get("collect_us", 0.0)
    upd = b.get("update_huds_us", 0.0)
    poll = b.get("frame_poll_us", 0.0)
    head = b.get("frame_head_us", 0.0)
    tail = b.get("frame_tail_us", 0.0)
    tot = upd + collect + poll + head + tail
    print(f"Draw     : update {upd:.0f} + collect {collect:.0f} + poll {poll:.0f} "
          f"+ head {head:.0f} + tail {tail:.0f} us/frame ({100.0*tot/budget:.1f}% of budget)")

    # Flags — the things you'd actually act on.
    flags = []
    low1 = b.get("lowfps_1pct", 0.0)
    if low1 and low1 < target:
        sev = "!!" if low1 < target * 0.5 else "!"
        flags.append(f"[{sev}] 1% low {low1:.0f} fps is under the {target} fps target "
                     f"(p99 frame {b.get('ft_p99_us','?')} us > {budget:.0f} us budget)")
    for c in cbs:
        if c["pct_bud"] >= 50.0:
            flags.append(f"[!] callback '{c['name']}' peak {c['peak_us']:.0f} us "
                         f"= {c['pct_bud']:.0f}% of one frame")
    if collect >= budget * 0.5:
        flags.append(f"[!] collectRenderData {collect:.0f} us = {100.0*collect/budget:.0f}% of one frame")
    if not flags:
        print("Verdict  : OK — nothing exceeds the 480fps budget thresholds.")
    else:
        print("Flags    :")
        for fl in flags:
            print("  " + fl)

    # Render handoff by HUD — which HUD hands the engine the most to draw. Text is
    # the priciest primitive, so the string leaders are the ones to trim for FPS.
    huds = rep.get("huds") or []
    if huds:
        tot_s = sum(h["strings"] for h in huds)
        tot_q = sum(h["quads"] for h in huds)
        top_s = sorted(huds, key=lambda h: h["strings"], reverse=True)[:3]
        top_q = sorted(huds, key=lambda h: h["quads"], reverse=True)[:3]
        print(f"Handoff by HUD (base, pre-shadow): {tot_s} strings, {tot_q} quads total")
        print("  top strings: " + ", ".join(f"{h['name']} {h['strings']}" for h in top_s if h['strings']))
        print("  top quads  : " + ", ".join(f"{h['name']} {h['quads']}" for h in top_q if h['quads']))

    # Whole-stint rebuild cost. This is the honest "which HUD cost me CPU this
    # session" answer: the footprint table's "Last us" is one rebuild's duration and
    # never decays, so on screen it reads like a constant per-frame cost when it is
    # not. Total = every rebuild summed; avg x count is what actually accumulated.
    sh = rep.get("stint_huds") or []
    if sh:
        dur = rep["bench"].get("dur_s") or 0.0
        print("Rebuild cost over the stint (heaviest first):")
        for h in sh[:5]:
            per_s = (h["total_us"] / dur) if dur else 0.0
            share = h.get("pct_update")
            tail = f"  {share:5.1f}% upd" if share is not None else ""
            print(f"  {h['name']:<22} {h['total_us']/1000.0:8.1f} ms total  "
                  f"{h['avg_us']:7.0f} us x {h['count']:<5d}  ~{per_s:6.0f} us/s{tail}")
        # THE PANELS THAT COST WITHOUT REBUILDING. `Idle us/f` is what a HUD spends on
        # the frames it is NOT dirty, and it is the one column the "heaviest rebuild"
        # ranking above cannot see: position_widget sat at 1.41us/frame with ZERO
        # rebuilds -- top of this list, absent from that one -- because its update()
        # polled the position cache every frame. Ranked separately so that shape shows.
        idle = [h for h in sh if h.get("idle_us_per_frame")]
        if idle:
            idle.sort(key=lambda h: h["idle_us_per_frame"], reverse=True)
            print("Idle cost (per frame, not rebuilding): " +
                  ", ".join(f"{h['name']} {h['idle_us_per_frame']:.2f}us"
                            f"{' (0 rebuilds)' if h['count'] == 0 else ''}"
                            for h in idle[:5]))
    if cbs:
        top = sorted(cbs, key=lambda c: c["total_us"], reverse=True)[:3]
        print("Callback cost over the stint: " +
              ", ".join(f"{c['name']} {c['total_us']/1000.0:.0f} ms" for c in top))
    return b


def analyze_compare(a, b):
    analyze_one(a)
    analyze_one(b)
    ba, bb = a["bench"], b["bench"]
    print("\n=== COMPARE (A -> B; +worse for frame-time/handoff, -better) ===")

    def row(label, key, unit="", better_low=True, pct=False):
        va, vb = ba.get(key), bb.get(key)
        if not isinstance(va, (int, float)) or not isinstance(vb, (int, float)):
            return
        d = vb - va
        # Regression marker: for frame-time / us / handoff, higher is worse;
        # for FPS, lower is worse.
        worse = (d > 0) if better_low else (d < 0)
        tag = "  <-- regression" if worse and abs(d) > 1e-9 else ""
        if pct and va:
            print(f"  {label:<16} {va:>10.1f} -> {vb:>10.1f}{unit}  ({d:+.1f}{unit}, {100.0*d/va:+.1f}%){tag}")
        else:
            print(f"  {label:<16} {va:>10.1f} -> {vb:>10.1f}{unit}  ({d:+.1f}{unit}){tag}")

    row("fps min", "fps_min", " fps", better_low=False)
    row("fps avg", "fps_avg", " fps", better_low=False)
    row("1% low", "lowfps_1pct", " fps", better_low=False)
    row("frame p99", "ft_p99_us", " us", better_low=True, pct=True)
    row("frame max", "ft_max_us", " us", better_low=True, pct=True)
    row("update huds", "update_huds_us", " us", better_low=True, pct=True)
    row("collect", "collect_us", " us", better_low=True, pct=True)
    row("frame poll", "frame_poll_us", " us", better_low=True, pct=True)
    row("frame tail", "frame_tail_us", " us", better_low=True, pct=True)
    row("hud input", "hud_input_us", " us", better_low=True, pct=True)
    row("plan chain", "plan_chain_us", " us", better_low=True, pct=True)
    row("quads peak", "quads_peak", "", better_low=True, pct=True)
    row("strings peak", "strings_peak", "", better_low=True, pct=True)


def analyze_trend(reps):
    print(f"\n=== TREND ({len(reps)} reports, oldest first) ===")
    hdr = f"{'file':<28} {'riders':>6} {'fps_avg':>8} {'1%low':>7} {'p99us':>7} {'quads_pk':>8}"
    print(hdr)
    print("-" * len(hdr))
    for r in reps:
        b = r["bench"]
        name = r["path"].split("/")[-1]
        if len(name) > 28:
            name = name[:27] + "…"
        print(f"{name:<28} {b.get('riders',0):>6} {b.get('fps_avg',0):>8.1f} "
              f"{b.get('lowfps_1pct',0):>7.1f} {b.get('ft_p99_us',0):>7.0f} {b.get('quads_peak',0):>8}")


def main():
    ap = argparse.ArgumentParser(description="Analyze MXBMRP3 benchmark reports.")
    ap.add_argument("files", nargs="+", help="benchmark_*.txt report file(s)")
    ap.add_argument("--compare", action="store_true",
                    help="compare exactly two reports (before after)")
    a = ap.parse_args()

    if a.compare:
        if len(a.files) != 2:
            ap.error("--compare needs exactly two files (before after)")
        analyze_compare(load(a.files[0]), load(a.files[1]))
    else:
        reps = [load(f) for f in a.files]
        if len(reps) == 1:
            analyze_one(reps[0])
        else:
            for r in reps:
                analyze_one(r)
            analyze_trend(reps)


if __name__ == "__main__":
    main()
