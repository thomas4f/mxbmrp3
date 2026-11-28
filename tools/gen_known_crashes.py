#!/usr/bin/env python3
# Generate the human-readable crash catalogue (KNOWN_GAME_CRASHES.md) from the
# machine-readable registry (known_game_crashes.json), which is the single source of
# truth. Edit the JSON, then run this to refresh the Markdown -- they can't drift.
#
#   python3 tools/gen_known_crashes.py
#
# Optional args: <registry.json> <output.md>
import datetime
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "crash_analysis", "known_game_crashes.json")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "crash_analysis", "KNOWN_GAME_CRASHES.md")

# The generated-doc note, in the one shape every generated page in this repo
# uses: VISIBLE and after the intro. It was an HTML comment, which is invisible
# in a rendered page - so the one reader it is addressed to, someone about to
# edit this on GitHub, was the one reader who could not see it.
HEADER = """# Known MX Bikes crashes

A quick reference for working out which MX Bikes crash you hit and whether there's a
workaround. These are crashes in the game itself, caught by the MXBMRP3 plugin's crash
handler. They are not plugin bugs, and only PiBoSo (the game's developer) can fix them.
(A few fault inside a shared component such as the Visual C++ runtime, but the bad input
still comes from the game.)

<sub>Generated from `crash_analysis/known_game_crashes.json` by `tools/gen_known_crashes.py` - do not edit. Edit the JSON, then run `python3 tools/gen_known_crashes.py`.</sub>
"""

HOWTO = """## Which crash did you hit?

1. Open **Event Viewer** (Start → type "Event Viewer") → **Windows Logs → Application**.
2. Find the most recent **Error** whose *Faulting application name* is `mxbikes.exe`
   (Event ID 1000).
3. Note the **Faulting module name** and **Fault offset** (e.g. `mxbikes.exe` +
   `0x00000000001f1ba3`, or `msvcr90.dll` + `0x0000000000036ede`).
4. Find that `module + offset` in the table below. The offset is written exactly as Event
   Viewer shows it (16 zero-padded hex digits), so you can paste the **Fault offset** value
   straight into your browser's Find (Ctrl+F) to jump to it. On an older game build, open
   **Older builds** at the end first.

**Can't find it?** The plugin also saves a crash dump to
`Documents\\PiBoSo\\MX Bikes\\mxbmrp3\\crashes\\`. Send that over privately (it can contain
your Windows user name, computer name and recent chat lines) and it can be identified
straight from the dump. It's matched by the crash's machine code, which stays the same even
when the offset moves between builds, then added here.
"""

FOOTER = """---
*Technical detail for each crash (faulting instruction, mechanism, byte signature,
reproduction) lives in `known_game_crashes.json`; see `README.md` to analyse a dump or extend
this list.*
"""

NO_WORKAROUND = "None known. Only PiBoSo can fix it."


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


def build_label(b, versions):
    # The version (if known) and compile date after the stamp Event Viewer shows.
    bits = [f"`{b}`"]
    if versions.get(b):
        bits.insert(0, versions[b])
    d = built_date(b)
    if d:
        bits.append(f"built {d}")
    return ", ".join(bits)


def anchor(name):
    """GitHub's heading anchor for a crash's section, so the usage report can link
    each crash straight to it. Same rule GitHub applies: lowercase, drop everything
    but letters, digits, spaces, hyphens and underscores, then spaces to hyphens."""
    return re.sub(r"[^\w\- ]", "", name.lower()).replace(" ", "-")


def link(c):
    return f"[{c['name']}](#{anchor(c['name'])})"


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


def section(c, first):
    # One crash, described once: a third-party crash is identified by the culprit
    # module on the stack (`identify_by`), every other by its fault offset.
    label = "Identify by" if c.get("identify_by") else "Fault offset"
    return [f"### {c['name']}", "",
            f"**{label}:** {first}", "",
            f"**When:** {c.get('when', '')}", "",
            f"**What to do:** {c.get('workaround') or NO_WORKAROUND}", ""]


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

    # All game builds present (hex TimeDateStamp keys), newest first. Only the
    # newest is written out in full: it is the one players run. The older ones
    # fold into offset-only tables at the end, kept for stragglers on old builds.
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

    out = [HEADER, "", HOWTO, ""]
    if not crashes:
        out += ["*No crashes catalogued yet.*", "", FOOTER]
        text = "\n".join(out).rstrip() + "\n"
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"wrote {OUT}: empty catalogue (no crashes)")
        return

    current = [c for c in game if latest in c.get("builds", {})] + modstable
    first = {c["id"]: offset_str(c, latest if c not in modstable else None, default_module)
             for c in current}

    out += ["## Crashes on the current build", ""]
    if latest:
        out += [f"*Current build: {build_label(latest, versions)}. Crashes in a shared component such as the "
                "Visual C++ runtime have the same offset on every build. Older builds are "
                "folded at the end.*", ""]
    out += ["| Fault offset | Crash |", "|---|---|"]
    out += [f"| {first[c['id']]} | {link(c)} |" for c in current]
    out.append("")
    for c in current:
        out += section(c, first[c["id"]])

    if third_party:
        out.append("## Not MX Bikes - third-party software")
        out.append("")
        out.append("*These faults are captured by the plugin's crash handler but are **not** MX Bikes "
                   "or plugin bugs - they come from other software injected into the game. Identify "
                   "them by the module on the crash stack; the exact fault offset is machine/Windows-"
                   "specific, so don't match on it.*")
        out.append("")
        for c in third_party:
            out += section(c, c.get("identify_by") or offset_str(c, None, default_module))

    older = builds_sorted[1:]
    if older:
        out += ["## Older builds", "",
                "<details>",
                "<summary>Fault offsets on earlier game builds, for anyone still running one. "
                "The crashes are the same ones described above.</summary>", ""]
        for b in older:
            out += [f"**{build_label(b, versions)}**", "",
                    "| Fault offset | Crash |", "|---|---|"]
            out += [f"| {offset_str(c, b, default_module)} | {link(c)} |"
                    for c in game if b in c.get("builds", {})]
            out.append("")
        out += ["</details>", ""]

    out.append(FOOTER)

    text = "\n".join(out).rstrip() + "\n"
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {OUT}: {len(crashes)} crashes, current build {latest}, "
          f"{len(older)} older build(s) folded")


if __name__ == "__main__":
    main()
