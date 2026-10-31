#!/usr/bin/env python3
# ============================================================================
# tests/integration/perf_alloc_sites.py
# WHERE the perf ledger's allocations come from. The driver, run with
# MXB_LEDGER_SITES=<file> (run_perf_ledger.sh --sites does this), writes one
# line per distinct allocating call stack: "tag count bytes off0 off1 ...",
# the tag being the callback (its tape event type) or Draw, the offsets
# return addresses relative to the test DLL's base. This resolves them against
# the DLL's symbol table and prints, per tag, the first frame of each stack
# that is plugin code rather than the standard library - the line to fix.
#
#   perf_alloc_sites.py SITES_FILE TEST_DLL [--min-share 0.005]
#   perf_alloc_sites.py --cpu PROFILE_FILE TEST_DLL [--top 40]
#
# --cpu reads perf_sampler.h's output instead (MXB_LEDGER_PROFILE=<file>):
# self and inclusive samples per plugin function. A frame outside the DLL
# prints as "@module <- caller". Under Wine a user32/win32u call is a
# wineserver round trip (GetForegroundWindow ~23 us here, ~1 us on Windows),
# so time charged to one is this host's, not a player's.
#
# Evaluated first: addr2line (the cross-build carries no line tables for the
# plugin's own TUs, so it answers "??"), and heaptrack / valgrind massif /
# gperftools (Linux-native; none of them can follow a PE DLL under Wine).
# ============================================================================
import bisect
import collections
import re
import subprocess
import sys

# tests/integration/harness/tape.h EventType, plus the driver's Draw tag.
TAGS = {
    3: "EventInit", 5: "RunInit", 6: "RunDeinit", 7: "RunStart", 8: "RunStop",
    9: "RunLap", 10: "RunSplit", 11: "RunTelemetry", 14: "TrackCenterline",
    15: "RaceEvent", 17: "RaceSession", 18: "RaceSessionState", 19: "RaceAddEntry",
    20: "RaceRemoveEntry", 21: "RaceLap", 22: "RaceSplit", 23: "RaceHoleshot",
    24: "RaceClassification", 25: "RaceTrackPosition", 26: "RaceCommunication",
    27: "RaceVehicleData", 1000: "Draw",
}

# Frames that are the allocator or the standard library: skipped, so each stack
# is charged to the plugin function that asked for the memory.
LIBRARY = re.compile(
    r"^(operator new|std::|__gnu_cxx|void std::|_Z|char\* std)|recordSite"
    r"|std::__cxx11::basic_string<|std::vector<.*>::_M_|std::_Hashtable"
    r"|_M_(realloc|emplace|create|construct|mutate|append|assign|replace|insert)")


def tool(name):
    return "x86_64-w64-mingw32-" + name


def symbols(dll):
    out = subprocess.run([tool("nm"), "-C", "--defined-only", dll],
                         capture_output=True, text=True, check=True).stdout
    table = []
    for line in out.splitlines():
        m = re.match(r"([0-9a-f]+) [tTwW] (.*)", line)
        if m:
            table.append((int(m.group(1), 16), m.group(2)))
    table.sort()
    return [a for a, _ in table], [n for _, n in table]


def cpu(argv):
    prof, dll = argv[0], argv[1]
    top = int(argv[argv.index("--top") + 1]) if "--top" in argv else 40
    header = subprocess.run([tool("objdump"), "-p", dll], capture_output=True, text=True, check=True).stdout
    base = int(re.search(r"ImageBase\s+([0-9a-f]+)", header).group(1), 16)
    addrs, names = symbols(dll)

    def name_at(off, leaf):
        # The leaf is the sampled instruction itself; every other frame is a
        # return address, which points just past its call.
        i = bisect.bisect_right(addrs, base + off - (0 if leaf else 1)) - 1
        return names[i][:110] if i >= 0 else "?"

    own, inclusive, total = collections.Counter(), collections.Counter(), 0
    with open(prof, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if not parts or parts[0] == "#":
                continue
            count, frames = int(parts[0]), parts[1:]
            named = [name_at(int(x, 16), i == 0) for i, x in enumerate(frames) if not x.startswith("@")]
            if not named:
                continue
            total += count
            own[named[0] if not frames[0].startswith("@") else frames[0] + " <- " + named[0]] += count
            for fn in set(named):
                inclusive[fn] += count
    for title, table in (("SELF", own), ("INCLUSIVE", inclusive)):
        print(f"== {title} ({total} samples with a plugin frame)")
        for fn, count in table.most_common(top):
            print(f"   {count:7d} {100.0 * count / total:5.1f}%  {fn}")
    return 0


def main(argv):
    if argv and argv[0] == "--cpu":
        return cpu(argv[1:])
    if len(argv) < 2:
        print(__doc__ or "usage: perf_alloc_sites.py SITES_FILE TEST_DLL [--min-share F]")
        return 2
    sites_path, dll = argv[0], argv[1]
    min_share = float(argv[argv.index("--min-share") + 1]) if "--min-share" in argv else 0.005
    header = subprocess.run([tool("objdump"), "-p", dll], capture_output=True, text=True, check=True).stdout
    base = int(re.search(r"ImageBase\s+([0-9a-f]+)", header).group(1), 16)
    addrs, names = symbols(dll)

    def name_at(off):
        # A return address points just past its call; step back into the caller.
        i = bisect.bisect_right(addrs, base + off - 1) - 1
        return names[i] if i >= 0 else "?"

    def owner(stack):
        for off in stack:
            fn = name_at(off)
            if not LIBRARY.search(fn):
                return fn[:110]
        return "?"

    per_site = collections.defaultdict(lambda: [0, 0])
    with open(sites_path, encoding="utf-8") as f:
        for line in f:
            parts = line.split()
            if len(parts) < 3:
                continue
            tag, count, nbytes = int(parts[0]), int(parts[1]), int(parts[2])
            stack = [int(x, 16) for x in parts[3:]]
            key = (TAGS.get(tag, str(tag)), owner(stack))
            per_site[key][0] += count
            per_site[key][1] += nbytes

    per_tag = collections.Counter()
    for (tag, _), (count, _) in per_site.items():
        per_tag[tag] += count
    for tag, total in per_tag.most_common():
        print(f"== {tag}: {total} allocations")
        for (t, site), (count, nbytes) in sorted(per_site.items(), key=lambda kv: -kv[1][0]):
            if t == tag and count >= total * min_share:
                print(f"   {count:9d} {nbytes:12d} B  {site}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
