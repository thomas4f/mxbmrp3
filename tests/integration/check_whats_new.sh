#!/usr/bin/env bash
# ============================================================================
# check_whats_new.sh
# Every what's-new marker names the CURRENT release line.
#
# WHY THIS GATE EXISTS. A "New" tag that is still lit two releases later is
# worse than no tag: it teaches players that the tag means nothing, and the next
# genuinely new thing gets the same shrug. The runtime already refuses to draw a
# marker whose version does not match (WhatsNew::isLive), so a forgotten entry
# is invisible rather than wrong -- but it is also DEAD CODE that reads like
# live code, and the next person to edit the table has to work out which rows
# still do anything.
#
# So the rule is: the table names this release, or the build fails. Pruning it
# is then part of cutting a release rather than something to remember.
#
# Deliberately NOT a check that the table is non-empty: a release with nothing
# worth marking is a legitimate release, and a gate that demands a marker would
# be a gate that manufactures noise.
#
# Bespoke because nothing off the shelf knows what a marker is; it is ~30 lines
# of grep against two files that must agree.
# ============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
TABLE="${ROOT}/mxbmrp3/hud/settings/whats_new.cpp"
RESOURCE="${ROOT}/mxbmrp3/resource.h"

for f in "${TABLE}" "${RESOURCE}"; do
    if [ ! -f "$f" ]; then
        echo "check_whats_new: missing $f" >&2
        exit 3   # CTest SKIPPED: the tree is not what this gate expects
    fi
done

major=$(grep -oP '#define\s+VER_MAJOR\s+\K[0-9]+' "${RESOURCE}" | head -1)
minor=$(grep -oP '#define\s+VER_MINOR\s+\K[0-9]+' "${RESOURCE}" | head -1)
if [ -z "${major}" ] || [ -z "${minor}" ]; then
    echo "check_whats_new: could not read VER_MAJOR/VER_MINOR from ${RESOURCE}" >&2
    exit 3
fi
current="${major}.${minor}"

# The version string of every marker row: the third field of each { ... } entry.
mapfile -t versions < <(grep -oP '\{\s*SettingsHud::TAB_\w+\s*,\s*"[^"]*"\s*,\s*"\K[^"]+' "${TABLE}")

fail=0
for v in "${versions[@]:-}"; do
    [ -z "$v" ] && continue
    if [ "$v" != "${current}" ]; then
        echo "check_whats_new: FAIL: marker version '${v}' is not the current release line '${current}'" >&2
        fail=1
    fi
done

if [ "${fail}" -ne 0 ]; then
    cat >&2 <<EOF

  mxbmrp3/hud/settings/whats_new.cpp holds markers from an earlier release.
  Cutting a release means deciding what is worth marking THIS time:
    - drop the rows whose feature is no longer new, and
    - bump the ones still worth pointing at, or add new ones,
  so every row reads "${current}".

  A marker that names an older line never draws (WhatsNew::isLive), so this is
  not a live bug -- it is dead rows that read like live ones.
EOF
    exit 1
fi

echo "check_whats_new: OK (${#versions[@]} marker(s), all on ${current})"
exit 0
