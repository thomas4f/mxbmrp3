#!/usr/bin/env python3
# Generate the human-readable crash catalogue (KNOWN_GAME_CRASHES.md) from the
# machine-readable registry (known_game_crashes.json), which is the single source of
# truth. Edit the JSON, then run this to refresh the Markdown -- they can't drift.
#
#   python3 tools/gen_known_crashes.py
#
# Optional args: <registry.json> <output.md>
import json, os, sys, datetime

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "crash_analysis", "known_game_crashes.json")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "crash_analysis", "KNOWN_GAME_CRASHES.md")

HEADER = """<!-- GENERATED from known_game_crashes.json by tools/gen_known_crashes.py -- DO NOT EDIT BY HAND.
     Edit known_game_crashes.json, then run: python3 tools/gen_known_crashes.py -->
# Known MX Bikes crashes

A quick reference for working out which MX Bikes crash you hit and whether there's a
workaround. These are crashes in the game itself, caught by the MXBMRP3 plugin's crash
handler. They are not plugin bugs, and only PiBoSo (the game's developer) can fix them.
(A few fault inside a shared component such as the Visual C++ runtime, but the bad input
still comes from the game.)
"""

HOWTO = """## Which crash did you hit?

1. Open **Event Viewer** (Start → type "Event Viewer") → **Windows Logs → Application**.
2. Find the most recent **Error** whose *Faulting application name* is `mxbikes.exe`
   (Event ID 1000).
3. Note the **Faulting module name** and **Fault offset** (e.g. `mxbikes.exe` +
   `0x00000000001f1923`, or `msvcr90.dll` + `0x0000000000036ede`).
4. Find that `module + offset` in the tables below. The offset is written exactly as Event
   Viewer shows it (16 zero-padded hex digits), so you can paste the **Fault offset** value
   straight into your browser's Find (Ctrl+F) to jump to it. Each offset belongs to one game
   build, so it appears in exactly one section.

**Can't find it?** The plugin also saves a crash dump to
`Documents\\PiBoSo\\MX Bikes\\mxbmrp3\\crashes\\`. Send that over and it can be identified
straight from the dump. It's matched by the crash's machine code, which stays the same even
when the offset moves between builds, then added here.
"""

FOOTER = """---
*A blank workaround means none is known yet. The same crash can appear under several builds at
different offsets, because PiBoSo's compiler relocates code each build; the offset is
per-build while the underlying bug is the same. Full technical detail (faulting instruction, mechanism,
byte signature, reproduction) lives in `known_game_crashes.json`; see `README.md` to analyse a dump
or extend this list.*
"""


def parse_tds(k):
    try:
        return int(k, 16)
    except (ValueError, TypeError):
        return None


def built_date(tds):
    v = parse_tds(tds)
    if v is None:
        return None
    try:
        return datetime.datetime.fromtimestamp(v, datetime.timezone.utc).strftime("%Y-%m-%d")
    except (OverflowError, OSError, ValueError):
        return None


def build_heading(b, versions, is_latest):
    # Lead with the build stamp (it's what Event Viewer shows as the module "time
    # stamp"), then the friendly context: version (if known) and compile date.
    bits = []
    if versions.get(b):
        bits.append(versions[b])
    d = built_date(b)
    if d:
        bits.append(f"built {d}")
    if is_latest:
        bits.append("newest seen")
    head = f"### Build `{b}`"
    return head + " · " + " · ".join(bits) if bits else head


def when_cell(c):
    # Plain, player-recognisable situation only. Technical detail and the
    # reproduction trigger stay in known_game_crashes.json (observed/summary/trigger).
    return c.get("when", "")


def fmt_offset(off):
    # Render the offset exactly as Event Viewer's "Fault offset" field does: 0x + 16
    # zero-padded hex digits. Registry stores a canonical compact form (e.g. "+0x1f1923");
    # padding here lets users Ctrl+F the raw event-log value. Falls back to the stored
    # string if it isn't parseable hex.
    s = off.lstrip("+").lower()
    if s.startswith("0x"):
        s = s[2:]
    try:
        return f"0x{int(s, 16):016x}"
    except (ValueError, TypeError):
        return off


def offset_str(c, build, default_module):
    # Module-qualified so a game offset reads the same way as a non-game one and
    # matches Event Viewer's module + offset. `build` None -> first (module-stable).
    mod = c.get("match", {}).get("module") or default_module
    builds = c.get("builds", {})
    off = builds.get(build) if build else next(iter(builds.values()), None)
    return f"`{mod}+{fmt_offset(off)}`" if off else "-"


def crash_row(c, build, default_module):
    wa = c["workaround"] if c.get("workaround") else "-"
    # A third-party crash (e.g. an injected overlay/capture hook) faults in a
    # system DLL whose offset is machine/Windows-specific -- so it is identified
    # by the culprit MODULE on the stack, not a fault offset. `identify_by` holds
    # that human signal and replaces the offset cell.
    first = c.get("identify_by") or offset_str(c, build, default_module)
    return f"| {first} | **{c['name']}** | {when_cell(c)} | {wa} |"


def table(rows, first_col="Fault offset"):
    return [f"| {first_col} | Crash | When | Workaround |", "|---|---|---|---|"] + rows + [""]


def main():
    with open(SRC, encoding="utf-8") as f:
        reg = json.load(f)
    default_module = reg.get("module", "mxbikes.exe")
    crashes = reg.get("crashes", [])
    versions = reg.get("build_versions", {})  # optional, human-filled: TDS -> "beta21e"

    # Split MX Bikes' own crashes from third-party ones (injected overlays/hooks
    # that fault in a system DLL and are identified by the culprit module, not an
    # offset). Third-party crashes get their own section, keyed by 'identify_by'.
    game = [c for c in crashes if not c.get("third_party")]
    third_party = [c for c in crashes if c.get("third_party")]

    # All game builds present (hex TimeDateStamp keys), newest first.
    build_val = {}
    for c in game:
        for k in c.get("builds", {}):
            v = parse_tds(k)
            if v is not None:
                build_val[k] = v
    builds_sorted = sorted(build_val, key=lambda k: build_val[k], reverse=True)
    latest = builds_sorted[0] if builds_sorted else None
    modstable = [c for c in game
                 if not any(parse_tds(k) is not None for k in c.get("builds", {}))]
    as_of = max((c.get("last_seen") for c in crashes if c.get("last_seen")), default=None)

    out = [HEADER, "", HOWTO, "", "## Crashes by build", ""]
    if not crashes:
        out += ["*No crashes catalogued yet.*", "", FOOTER]
        text = "\n".join(out).rstrip() + "\n"
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"wrote {OUT}: empty catalogue (no crashes)")
        return
    note = "Newest build first."
    if as_of:
        note += f" Catalogue current as of {as_of}."
    out += [f"*{note}*", ""]

    for b in builds_sorted:
        out.append(build_heading(b, versions, b == latest))
        out.append("")
        out += table([crash_row(c, b, default_module)
                      for c in game if b in c.get("builds", {})])

    if modstable:
        out.append("### Build-independent (faults in a shared component, same offset on every build)")
        out.append("")
        out += table([crash_row(c, None, default_module) for c in modstable])

    if third_party:
        out.append("## Not MX Bikes — third-party software")
        out.append("")
        out.append("*These faults are captured by the plugin's crash handler but are **not** MX Bikes "
                   "or plugin bugs — they come from other software injected into the game. Identify "
                   "them by the module on the crash stack; the exact fault offset is machine/Windows-"
                   "specific, so don't match on it.*")
        out.append("")
        out += table([crash_row(c, None, default_module) for c in third_party],
                     first_col="Identify by")

    out.append(FOOTER)

    text = "\n".join(out).rstrip() + "\n"
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {OUT}: {len(crashes)} crashes across {len(builds_sorted)} builds "
          f"(+{len(modstable)} build-independent)")


if __name__ == "__main__":
    main()
