#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_hud_raw_cache.sh
# Raw-game-data lint for HUD headers (no compiler needed, pure grep/awk).
#
# THE INVARIANT (CLAUDE.md "Design Decisions"): HUDs cache FORMATTED render
# data — quads, strings, display rows — and read game STATE from PluginData at
# rebuild. A HUD keeping its own copy of state PluginData already owns has
# duplicated it, and the copy goes stale in exactly the situations nobody tests
# by hand: a rider leaving mid-session, a session reset, a spectate change.
#
# READ THIS BEFORE "FIXING" THE EXEMPT HUDS. Two of the three are not holding a
# second copy of anything, because two DIFFERENT structs are involved:
#
#   Unified::TrackPositionData  raceNum, posX/posY/posZ, yaw, trackPos, crashed
#   RiderTrackState             trackPos, numLaps, sessionTime, crashed,
#                               wrong-way + hazard + crash-count state
#
# handleRaceTrackPosition() forwards five scalars into PluginData and DROPS the
# world coordinates. Map and Radar read posX/posZ/yaw (map_hud_geometry.cpp,
# map_hud_riders.cpp, radar_hud.cpp) and MapHud also holds the centreline, which
# PluginData never stores at all — so for those two there is no other source, and
# they are not circumventing the single-source rule. PluginData is a race-logic
# store on purpose, not a mirror of every field the API sends.
#
# GapBar is the honest exception to that defence: its opponent loop reads only
# raceNum and trackPos, both of which PluginData DOES store. That copy is a
# convenience, not a necessity, and its annotation says so. Don't read the three
# exemptions as one rule — check which fields your case actually needs.
#
# All three are safe because each uses assign() to replace the batch wholesale,
# so staleness cannot accumulate — which is also why they need no PerRider<>
# eviction, unlike PluginData's own m_trackPositions.
#
# So: a NEW `Unified::` member in a HUD header fails here — not because raw
# types are banned, but because the question "does PluginData already own this
# state?" is worth answering out loud. If it genuinely does not, add
# `// raw-cache: <reason>` on the same line or the line above, the same
# annotation shape check_visibility_gates.sh uses for `vis-gate:`.
#
# Not matched (by design), because none of these is a HUD keeping a copy:
#   - function PARAMETERS and return types (a HUD handed
#     `const Unified::TrackPositionData*` to consume immediately is the correct
#     shape — CompassWidget does exactly this and caches nothing);
#   - mentions inside comments;
#   - a REFERENCE or POINTER bound to an initializer (`const Unified::X& r =
#     riders[i];`). That aliases storage someone else owns, so it cannot go
#     stale independently. Header-only helpers in this directory loop over a
#     caller's array that way (radar_fade.h), and flagging the loop body told
#     them to annotate a local as if it were a cache.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <file> -> the offending lines, one per line (empty output == clean).
# Both the real run and --self-test go through this, so the self-test exercises
# the shipping matcher rather than a copy of it.
scan() {
    awk '
        # Strip // comments so a mention in prose never trips the lint, but keep
        # the raw line for the annotation check and the error message.
        {
            raw = $0
            line = $0
            sub(/\/\/.*$/, "", line)
        }
        {
            stripped = raw
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", stripped)
            is_comment = (stripped ~ /^\/\//)
            is_blank = (stripped == "")
        }
        {
            # A member DECLARATION mentions Unified:: and ends in ; with no
            # parentheses. Parentheses mean a function signature - a parameter
            # or return type, which is the shape we want HUDs to use.
            # A `&`/`*` immediately before the declared name, with an
            # initializer, is a reference/pointer alias to storage owned
            # elsewhere — not a copy that can go stale. See the header.
            #
            # ...UNLESS the name is `m_`-prefixed, i.e. a MEMBER. The shape alone
            # cannot tell a loop-body local from a class member, and the member is
            # the case that most needs catching: m_riderPositions.assign()
            # reallocates, so a cached pointer into that batch DANGLES rather than
            # merely going stale. The m_ naming convention is the discriminator
            # that a regex over a single line can actually apply.
            # (No apostrophes in this awk program: it is single-quoted, so one
            # ends the string and bash reports a syntax error at the NEXT paren.)
            is_alias = (line ~ /[&*][[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=/ \
                        && line !~ /[&*][[:space:]]*m_/)
            # Parentheses normally mean a function signature -- a parameter or return
            # type, the shape HUDs SHOULD use. But an m_-prefixed name on the line means
            # it is a member no matter what else is there, which reopens two shapes the
            # paren rule alone let through:
            #     Unified::X m_x(0);                       paren-initialised
            #     std::vector<Unified::X> m_x = build();   initialised from a call
            # NOT \bm_ : \b is not a word boundary in POSIX awk (gawk spells it \y and
            # mawk does not have it at all), so the whole rule silently matched nothing.
            has_member = (line ~ /(^|[^A-Za-z0-9_])m_[A-Za-z0-9_]/)
            # A declaration split across lines puts Unified:: and the ; on DIFFERENT
            # lines, so neither matches alone:
            #     std::vector<Unified::X>
            #         m_x;
            # `pending` carries the type line forward. Set only when the line has no
            # parenthesis, so a wrapped function signature (radar_fade.h has one) never
            # arms it.
            if (line ~ /Unified::/ && line !~ /;[[:space:]]*$/ && line !~ /[()]/) {
                pending = 1
                pending_raw = raw
            } else if (pending && line ~ /;[[:space:]]*$/ && line !~ /[()]/ && has_member) {
                if (!(raw ~ /raw-cache:/) && !block_annotated) {
                    printf "  %s:%d:%s\n", FILENAME, FNR, pending_raw " " raw
                }
                pending = 0
            } else if (line !~ /^[[:space:]]*$/) {
                pending = 0
            }
            if (line ~ /Unified::/ && line ~ /;[[:space:]]*$/ \
                && (line !~ /[()]/ || has_member) && !is_alias) {
                # Annotated if the marker is on this line, or ANYWHERE in the
                # contiguous comment block directly above it. A one-line window
                # (what check_visibility_gates.sh uses) is too tight here: these
                # reasons have to name which PluginData fields are missing, which
                # does not fit on one line, and forcing the marker onto the last
                # line just to satisfy the lint made the comments read backwards.
                if (!(raw ~ /raw-cache:/) && !block_annotated) {
                    printf "  %s:%d:%s\n", FILENAME, FNR, raw
                }
            }
            # Maintain the preceding-comment-block state AFTER the check, so a
            # declaration never counts as part of its own annotation block.
            if (is_comment) {
                if (raw ~ /raw-cache:/) block_annotated = 1
            } else if (!is_blank) {
                block_annotated = 0
            }
        }
    ' "$1"
}

# --------------------------------------------------------------------------
# SELF-TEST. These cases were once "mutation-tested" by hand and the result
# asserted in a commit message; the assertion was WRONG (the m_-prefixed pointer
# and reference cases were never actually run, and the exemption let them
# through). Worse, the ad-hoc way of running them -- append a line, look at the
# exit code -- reports every case as CAUGHT the moment the script has a syntax
# error, because a dead script also exits non-zero. So the cases live here, they
# assert on the MATCHER OUTPUT rather than an exit code, and the gate runs them.
#
# This is the "one test for a gate" that CLAUDE.md calls normal, not a lint
# checking a lint: same file, same matcher, no second implementation to keep in
# step.
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.h"
    st_fail=0

    # expect <must-be-flagged: yes|no> <label> <line...>
    expect() {
        local want="$1" label="$2"; shift 2
        printf '%s\n' "$@" > "${fixture}"
        local got; got=$(scan "${fixture}" || true)
        local flagged=no; [[ -n "${got}" ]] && flagged=yes
        if [[ "${flagged}" != "${want}" ]]; then
            echo "  SELF-TEST FAIL [${label}]: expected flagged=${want}, got ${flagged}"
            st_fail=1
        fi
    }

    # Must be caught: every shape of a HUD keeping its own copy.
    expect yes "plain member"        '    std::vector<Unified::TrackPositionData> m_copy;'
    expect yes "member = initializer" '    std::vector<Unified::TrackPositionData> m_copy = {};'
    expect yes "brace-init member"   '    Unified::SessionData m_copy{};'
    expect yes "wrapped declaration" '    std::vector<' '        Unified::SessionData> m_copy;'
    expect yes "type-alias line"     '    using P = Unified::TrackPositionData;'
    # The two the over-broad alias exemption used to wave through. A pointer into
    # a batch replaced by assign() dangles, so this is the worst case, not an edge.
    expect yes "POINTER member"      '    const Unified::TrackPositionData* m_cached = nullptr;'
    expect yes "REFERENCE member"    '    const Unified::TrackPositionData& m_cached = *p;'
    # An alias line must not shelter a real member declared after it.
    # The three shapes the paren rule and the single-line assumption let through
    # until a review found them. All are ordinary ways to write a member.
    expect yes "paren-initialised"   '    Unified::SessionData m_copy(0);'
    expect yes "init from a call"    '    std::vector<Unified::SessionData> m_copy = build();'
    expect yes "split declaration"   '    std::vector<Unified::SessionData>' '        m_copy;'
    expect yes "member after alias"  '    const Unified::SessionData& r = other[i];' \
                                     '    Unified::SessionData m_copy;'

    # Must NOT be caught: consuming the type without owning a copy.
    expect no  "loop-body alias"     '    const Unified::TrackPositionData& r = riders[i];'
    expect no  "function parameter"  '    void update(const Unified::TrackPositionData* p);'
    expect no  "wrapped signature"   '    inline float f(const Unified::TrackPositionData* riders,' \
                                     '                   int count) {'
    expect no  "wrapped sig then body" '    inline float f(const Unified::TrackPositionData* r,' \
                                       '                   int n) {' '        int m_unrelated;'
    expect no  "mention in a comment" '    // Unified::TrackPositionData m_copy;'
    expect no  "annotated member"    '    // raw-cache: world coords live nowhere else' \
                                     '    std::vector<Unified::TrackPositionData> m_riderPositions;'
    expect no  "annotated same line" '    std::vector<Unified::TrackSegment> m_segs;  // raw-cache: geometry'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "HUD raw-cache lint SELF-TEST FAILED."
        exit 1
    fi
    echo "HUD raw-cache lint self-test clean (16 cases)."
    exit 0
fi

fail=0
checked=0

for f in mxbmrp3/hud/*.h; do
    checked=$((checked + 1))
    out=$(scan "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

HUD RAW-CACHE LINT FAILED.
Each line above declares a `Unified::` member in a HUD header. Check first
whether PluginData already owns that state: if it does, read it there at
rebuild rather than keeping a second copy that can go stale.

If it does NOT — the case the exempt HUDs are in, where PluginData keeps a
different projection and the fields you need (world coords, track geometry)
live nowhere else — then a member here is correct. Say so with an annotation
naming what PluginData lacks, and replace the batch wholesale (assign()) so
the copy cannot accumulate staleness:

    std::vector<Unified::TrackSegment> m_trackSegments;  // raw-cache: <reason>
EOF
    exit 1
fi
echo "HUD raw-data caching clean (${checked} headers checked)."
