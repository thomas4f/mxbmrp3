#!/usr/bin/env python3
# ============================================================================
# tests/integration/persist_check.py <expect.txt> <resaved.ini>
# Verifies every perturbed setting survived the plugin's load -> save round-trip.
# A value that is now absent or reverted is a persistence failure (a user change
# that would not survive a restart).
# ============================================================================
import sys, re

expect_path, resaved_path = sys.argv[1], sys.argv[2]

# Parse the re-saved INI into {(section, key): value}.
actual = {}
section = ""
for line in open(resaved_path, "r", encoding="utf-8", errors="replace").read().splitlines():
    sm = re.match(r"^\s*\[([^\]]+)\]", line)
    if sm: section = sm.group(1); continue
    m = re.match(r"^([^;#=\s][^=]*)=([^;\r\n]*)", line)
    if m: actual[(section, m.group(1).strip())] = m.group(2).strip()

expected = [tuple(l.split("\t")) for l in open(expect_path).read().splitlines() if l.strip()]

reverted, missing, ok = [], [], 0
for sec, key, want in expected:
    got = actual.get((sec, key))
    if got is None:
        missing.append((sec, key, want))          # section/key not written back at all
    elif got != want:
        reverted.append((sec, key, want, got))     # written, but not our value
    else:
        ok += 1

total = len(expected)
print(f"persistence round-trip: {ok}/{total} settings survived")
if missing:
    print(f"\n{len(missing)} setting(s) NOT WRITTEN BACK (lost on restart):")
    for sec, key, want in missing[:40]:
        print(f"  [{sec}] {key}  (set to {want}, absent from re-saved file)")
if reverted:
    print(f"\n{len(reverted)} setting(s) REVERTED (written with a different value):")
    for sec, key, want, got in reverted[:40]:
        print(f"  [{sec}] {key}  set={want} but saved={got}")

sys.exit(0 if not missing and not reverted else 1)
