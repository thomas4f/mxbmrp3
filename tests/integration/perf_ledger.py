#!/usr/bin/env python3
# ============================================================================
# tests/integration/perf_ledger.py
# The comparison half of the perf ledger (run_perf_ledger.sh drives it).
# Reads the driver's output files, one per run, and:
#   1. requires every COUNT/HUD line to be IDENTICAL across repeats of a
#      profile - the ledger's whole claim is that counters are exact, so a
#      drift is a harness bug (an unpatched clock, a race) and fails loudly;
#   2. compares those lines against the committed baseline and fails on ANY
#      change, better or worse, so the PR that moves a counter carries the
#      baseline update and its diff shows the before/after;
#   3. prints the timings as the median over the repeats with their spread -
#      reported, never gated (run_perf.sh keeps the coarse timing gates) - and
#      the per-HUD pass's time per HUD, the costliest first.
#
#   perf_ledger.py BASELINE OUT_LEDGER PROFILE:RUN.txt [PROFILE:RUN.txt ...] [--update]
#   BASELINE "-" checks the repeats and prints the timings without comparing
#   (a tape that has no baseline, such as a real capture).
#
# Evaluated first: pytest-benchmark / asv / hyperfine. All of them time a
# command or a Python callable and compare timings; none compares exact
# counters emitted by a Wine-hosted C++ driver, which is the part that gates.
# ============================================================================
import os
import statistics
import sys
from collections import defaultdict


def hud_times(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("HUDTIME "):
                name, *fields = line[8:].split()
                v = {k: int(x) for k, x in (y.split("=") for y in fields)}
                rows.append((name, v["rebuild_us"], v["update_us"]))
    return rows


def parse(path):
    counts, huds, times = [], [], {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("COUNT "):
                counts.append(line[6:])
            elif line.startswith("HUD "):
                huds.append(line[4:])
            elif line.startswith("TIME "):
                name, *fields = line[5:].split()
                times[name] = {k: float(v) for k, v in (x.split("=") for x in fields)}
    return counts, huds, times


def ledger_lines(profile, counts, huds):
    return [f"{profile} {c}" for c in counts] + [f"{profile} HUD {h}" for h in huds]


def key_of(line):
    # "<profile> <key> <value>" or "<profile> HUD <name> rebuilds=.. quads=.. strings=.."
    parts = line.split()
    return " ".join(parts[:3]) if parts[1] == "HUD" else " ".join(parts[:2])


def main(argv):
    update = "--update" in argv
    # Allocation counts follow the libstdc++ that was linked in, so a baseline
    # is exact only for the toolchain that wrote it (see the gate below).
    toolchain = os.environ.get("LEDGER_TOOLCHAIN", "unknown")
    args = [a for a in argv if a != "--update"]
    if len(args) < 3:
        print("usage: perf_ledger.py BASELINE|- OUT PROFILE:FILE ... [--update]")
        return 2
    baseline_path, out_path, runs = args[0], args[1], args[2:]

    by_profile = defaultdict(list)
    for spec in runs:
        profile, path = spec.split(":", 1)
        by_profile[profile].append(path)

    failed = False
    repeats = 0
    ledger = []
    timing_rows = []
    for profile, paths in by_profile.items():
        if profile == "hudtime":
            continue          # real-clock pass: only its times are read, below
        parsed = [parse(p) for p in paths]
        main_runs = [p for p in parsed if not p[1]]       # the HUD pass carries HUD rows
        hud_runs = [p for p in parsed if p[1]]
        repeats = max(repeats, len(main_runs))
        if main_runs:
            first = main_runs[0][0]
            for i, other in enumerate(main_runs[1:], 2):
                if other[0] != first:
                    failed = True
                    print(f"== LEDGER FAIL: {profile} counters differ between run 1 and run {i} "
                          f"(the ledger is only worth anything if these are exact) ==")
                    for a, b in zip(first, other[0]):
                        if a != b:
                            print(f"   run1: {a}\n   run{i}: {b}")
            ledger += ledger_lines(profile, first, [])
        if hud_runs:
            for i, other in enumerate(hud_runs[1:], 2):
                if other[1] != hud_runs[0][1]:
                    failed = True
                    print(f"== LEDGER FAIL: {profile} per-HUD rows differ between pass 1 and pass {i} ==")
                    for a, b in zip(hud_runs[0][1], other[1]):
                        if a != b:
                            print(f"   pass1: {a}\n   pass{i}: {b}")
            ledger += ledger_lines(profile, [], hud_runs[0][1])
        for name in sorted({n for r in main_runs for n in r[2]}):
            vals = [r[2][name] for r in main_runs if name in r[2]]
            avg = [v["avg"] for v in vals]
            p99 = [v["p99"] for v in vals]
            timing_rows.append((profile, name, int(vals[0]["calls"]), statistics.median(avg),
                                min(avg), max(avg), statistics.median(p99)))

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(ledger) + "\n")

    for path in by_profile.get("hudtime", []):
        rows = sorted(hud_times(path), key=lambda r: -r[2])
        total = sum(r[2] for r in rows) or 1
        print("\n== PER-HUD TIME (all profile, real clock, whole replay, us; reported, not gated) ==")
        print(f"{'hud':24} {'update()':>12} {'of which rebuilds':>18} {'share':>7}")
        for name, rebuild_us, update_us in rows[:15]:
            print(f"{name:24} {update_us:12d} {rebuild_us:18d} {update_us / total * 100:6.1f}%")

    print("\n== TIMINGS (us, median of %d run(s) per profile; reported, not gated) ==" % repeats)
    print(f"{'profile':9} {'what':28} {'calls':>7} {'avg':>9} {'avg min-max':>17} {'p99':>9}")
    for profile, name, calls, avg, lo, hi, p99 in timing_rows:
        print(f"{profile:9} {name:28} {calls:7d} {avg:9.2f} {lo:8.2f}-{hi:<8.2f} {p99:9.2f}")

    if update:
        with open(baseline_path, "w", encoding="utf-8") as f:
            f.write("# Perf ledger baseline: exact counters from run_perf_ledger.sh on the\n"
                    "# synthetic busy tape (make_perf_tape.cpp). Regenerate with\n"
                    "#   ./tests/integration/run_perf_ledger.sh --update\n"
                    "# in the same commit as the change that moves them.\n"
                    f"# toolchain: {toolchain}\n")
            f.write("\n".join(ledger) + "\n")
        print(f"\n== LEDGER baseline written: {baseline_path} ({len(ledger)} lines) ==")
        return 1 if failed else 0

    if baseline_path == "-":
        print(f"\n== LEDGER written: {out_path} ({len(ledger)} lines; no baseline to compare) ==")
        return 1 if failed else 0
    try:
        with open(baseline_path, encoding="utf-8") as f:
            raw = [line.rstrip("\n") for line in f]
    except FileNotFoundError:
        print(f"== LEDGER FAIL: no baseline at {baseline_path} (run with --update) ==")
        return 1
    base = [line for line in raw if line.strip() and not line.startswith("#")]
    base_tc = next((line.split(":", 1)[1].strip() for line in raw if line.startswith("# toolchain:")), "unknown")
    same_toolchain = base_tc == toolchain
    base_map = {key_of(line): line for line in base}
    now_map = {key_of(line): line for line in ledger}
    changed = []
    for k in sorted(set(base_map) | set(now_map)):
        if base_map.get(k) != now_map.get(k):
            changed.append((k, base_map.get(k, "(absent)"), now_map.get(k, "(absent)")))
    # In CI a mismatch fails too: the runner image moving on (a newer mingw or
    # Wine) would otherwise turn the gate silently green for good. Locally it
    # only warns, since a developer's toolchain legitimately differs.
    strict = same_toolchain or bool(os.environ.get("GITHUB_ACTIONS"))
    if changed:
        if strict:
            failed = True
        print(f"\n== LEDGER: {len(changed)} counter(s) differ from the baseline ==")
        print(f"{'counter':52} {'baseline':>22} {'now':>22} {'change':>8}")
        for k, b, n in changed:
            bv = b.split(maxsplit=2)[-1] if b != "(absent)" else b
            nv = n.split(maxsplit=2)[-1] if n != "(absent)" else n
            if k.split()[1] == "HUD":
                bv = b.split(maxsplit=3)[-1] if b != "(absent)" else b
                nv = n.split(maxsplit=3)[-1] if n != "(absent)" else n
            pct = ""
            try:
                x, y = float(bv), float(nv)
                pct = f"{(y - x) / x * 100:+.1f}%" if x else ""
            except ValueError:
                pass
            print(f"{k:52} {bv:>22} {nv:>22} {pct:>8}")
        if same_toolchain:
            print("== If the change is intended, update the baseline in the same commit:\n"
                  "   ./tests/integration/run_perf_ledger.sh --update ==")
        elif strict:
            print(f"== LEDGER FAIL (CI): baseline toolchain is '{base_tc}', this runner's is "
                  f"'{toolchain}'; refresh the baseline on this toolchain (--update) ==")
        else:
            # A different mingw/libstdc++ allocates differently, so locally a
            # mismatch says nothing about the change under test. Reported, not
            # failed: gating on it would only teach people to ignore the gate.
            print(f"== LEDGER WARN (not failed): baseline toolchain is '{base_tc}', this one is "
                  f"'{toolchain}'; counters are exact only on the toolchain that wrote them ==")
    else:
        print(f"\n== LEDGER OK: all {len(ledger)} counters match the baseline ==")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
