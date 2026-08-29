#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_change_consumers.sh
# onDataChanged-consumer lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT (CLAUDE.md "Maintenance Invariants"): PluginData::notify fans
# every DataChangeType out to each consumer, and DataChangeType::Standings
# fires many times per second on a full grid — so every onDataChanged
# DEFINITION must either be trivially cheap or gate on consumer activity
# BEFORE any string/alloc work (the two models: HttpServer::hasActiveClients()
# and SteamFriendsManager's POD fingerprint). The dirty-flag work HudManager
# does is the "trivially cheap" shape.
#
# The lint cannot measure cost, so it does the next best thing: it makes the
# author ANSWER the cost question out loud. Every `X::onDataChanged(...)`
# definition must carry a `// change-gate: <how this stays cheap on the
# frequent types>` annotation on the definition line or in the contiguous
# comment block directly above it (the block rule check_hud_raw_cache.sh
# uses — these reasons rarely fit one line). A wrong reason is review's to
# catch; a missing one no longer is.
#
# Not matched (by design): call sites (`.onDataChanged(` / a qualified call in
# an expression), header declarations (no `::`), and comment-only mentions.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <file> -> offending definition lines (empty output == clean). Both the
# real run and --self-test go through this, so the self-test exercises the
# shipping matcher rather than a copy of it.
scan() {
    awk '
        {
            raw = $0
            line = $0
            sub(/\/\/.*$/, "", line)
            stripped = raw
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", stripped)
            is_comment = (stripped ~ /^\/\//)
            is_blank = (stripped == "")
        }
        {
            # A DEFINITION qualifies the name (`X::onDataChanged`) and opens a
            # body rather than ending in `;`. Call sites reach the method via
            # `.`/`->` on an instance (getInstance().onDataChanged(...)), so the
            # `::` immediately before the name is the discriminator; a character
            # before `X` other than start-of-line/space/`*`/`&` (e.g. `.`) means
            # an expression, not a definition.
            is_def = (line ~ /(^|[[:space:]*&])[A-Za-z_][A-Za-z0-9_]*::onDataChanged[[:space:]]*\(/ \
                      && line !~ /;[[:space:]]*$/)
            if (is_def && !(raw ~ /change-gate:/) && !block_annotated) {
                printf "  %s:%d: %s\n", FILENAME, FNR, raw
            }
            # Preceding-comment-block state, maintained AFTER the check so a
            # definition never counts as its own annotation block. Any
            # non-comment line — a blank included — breaks contiguity: a doc
            # block separated from its function does not belong to it.
            if (is_comment) {
                if (raw ~ /change-gate:/) block_annotated = 1
            } else {
                block_annotated = 0
            }
        }
    ' "$1"
}

# --------------------------------------------------------------------------
# SELF-TEST — the gate's own must-catch/must-exempt cases, asserting on the
# matcher output (same matcher as the real run, no second implementation).
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.cpp"
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

    expect yes "bare definition"     'void HudManager::onDataChanged(DataChangeType t) {'
    expect yes "unrelated comment"   '// fans changes out to the HUDs' \
                                     'void HudManager::onDataChanged(DataChangeType t) {'
    expect yes "blank between block" '// change-gate: cheap' '' \
                                     'void HudManager::onDataChanged(DataChangeType t) {'
    expect no  "annotated same line" 'void X::onDataChanged(DataChangeType t) {  // change-gate: flag flip only'
    expect no  "annotated block"     '// change-gate: gates on hasActiveClients() before' \
                                     '// any string work.' \
                                     'void X::onDataChanged(DataChangeType t) {'
    expect no  "call site"           '    HttpServer::getInstance().onDataChanged(changeType);'
    expect no  "header declaration"  '    void onDataChanged(DataChangeType changeType);'
    expect no  "comment mention"     '    // X::onDataChanged(t) is where this fans out'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "onDataChanged-consumer lint SELF-TEST FAILED."
        exit 1
    fi
    echo "onDataChanged-consumer lint self-test clean (8 cases)."
    exit 0
fi

fail=0
checked=0

# Recursive walk with a vendor prune (same shape as check_thread_join.sh), not
# fixed per-directory globs: a consumer defined in a subdirectory — a
# hud/settings/ tab, or any future TU split — must not escape the lint, which
# is exactly the silent-miss failure mode it exists to close.
while IFS= read -r f; do
    checked=$((checked + 1))
    out=$(scan "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done < <(find mxbmrp3 -path mxbmrp3/vendor -prune -o -name '*.cpp' -print | sort)

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

ONDATACHANGED-CONSUMER LINT FAILED.
Each line above defines an onDataChanged consumer without a change-gate
annotation. DataChangeType::Standings fires many times per second on a full
grid, so the consumer must be trivially cheap OR gate on consumer activity
before any string/alloc work (models: HttpServer::hasActiveClients(),
SteamFriendsManager's POD fingerprint — see CLAUDE.md Maintenance
Invariants). Say which it is, on the definition line or the comment block
above it:

    // change-gate: <how this stays cheap on the frequent types>
    void MyManager::onDataChanged(DataChangeType changeType) {
EOF
    exit 1
fi
echo "onDataChanged consumers clean (${checked} sources checked)."
