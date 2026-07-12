#!/usr/bin/env python3
# ============================================================================
# tests/integration/persist_gen.py <baseline.ini> <out.ini> <expect.txt>
# Generates a "perturbed" settings.ini for the persistence round-trip test:
# every boolean-ish value (0/1) is flipped, modelling a user toggling settings
# in the plugin (HUD visibility, feature toggles, column on/off, ...). Records
# what each value was flipped to so the checker can confirm it survived a
# load -> live-state -> save -> reload round-trip.
#
# A flipped value that reverts on the round-trip is a persistence bug: the
# setting was applied in memory but never written back (the hudOrder "third
# hardcoded list" trap that caused the FriendsHud silent-revert bug).
# ============================================================================
import sys, re

baseline, out, expect = sys.argv[1], sys.argv[2], sys.argv[3]

# Keys we must NOT flip:
#  - autoSave: off would disable the shutdown re-save the whole test relies on.
#  - scale/opacity/alpha: these are floats with a valid-range clamp (e.g. a
#    scale of 0 is invalid and correctly normalizes to the 0.1 minimum), so
#    flipping a "1" to "0" is a legitimate clamp, not a persistence failure.
#  - activeProfile / autoSwitch: navigation state, not a user "setting"; flipping
#    the active profile would change which profile the reset scope tests observe.
SKIP = {"autoSave", "activeProfile", "autoSwitch"}
SKIP_SUBSTR = ("scale", "opacity", "alpha")  # case-insensitive
def skip_key(key):
    kl = key.lower()
    return key in SKIP or any(s in kl for s in SKIP_SUBSTR)

lines = open(baseline, "r", encoding="utf-8", errors="replace").read().splitlines()
section = ""
records = []   # (section, key, newval)
out_lines = []
for line in lines:
    sm = re.match(r"^\s*\[([^\]]+)\]", line)
    if sm:
        section = sm.group(1); out_lines.append(line); continue
    m = re.match(r"^([^;#=\s][^=]*)=([^;\r\n]*)(\s*;.*)?$", line)
    if not m:
        out_lines.append(line); continue
    key, val, comment = m.group(1).strip(), m.group(2).strip(), (m.group(3) or "")
    if not skip_key(key) and val in ("0", "1"):
        newval = "1" if val == "0" else "0"
        records.append((section, key, newval))
        out_lines.append(f"{key}={newval}{comment}")
    else:
        out_lines.append(line)

with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(out_lines) + "\n")
with open(expect, "w", encoding="utf-8") as f:
    for sec, key, newval in records:
        f.write(f"{sec}\t{key}\t{newval}\n")
print(f"perturbed {len(records)} boolean settings across sections")
