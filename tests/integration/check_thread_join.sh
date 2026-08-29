#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_thread_join.sh
# Worker-thread join-site lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT (CLAUDE.md "Maintenance Invariants"): a worker thread must not
# outlive the DLL, and a destructor that join()s at DLL detach DEADLOCKS —
# FreeLibrary holds the loader lock the exiting worker needs, so the game
# HANGS. The game does unload without calling Shutdown() (the path two shipped
# crashes came from), so every std::thread must be joined by the orchestrated
# Shutdown() chain (PluginManager::shutdown and the paths it drives), with the
# destructor at most a spinThenDetach/spin backstop (thread_detach_grace.h,
# PluginThread's m_workerFinished).
#
# The lint cannot trace a join, so it makes the author NAME it: every
# `std::thread` member declaration in first-party code must carry a
# `// joined-by: <who joins it on the Shutdown path>` annotation on the line
# or in the contiguous comment block directly above. A wrong claim is
# review's to catch; a thread with no stated join site no longer is — that
# silence is exactly how the two shipped teardown crashes got in.
#
# Not matched (by design): function parameters and constructions (lines with
# parentheses — `spinThenDetach(std::thread&...)`, `m_t = std::thread(...)`),
# `std::thread::id`/`std::thread::` statics, comment mentions, and vendored
# code (`mxbmrp3/vendor/` — httplib owns its own pool teardown).
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <file> -> offending declarations (empty output == clean). Both the real
# run and --self-test go through this, so the self-test exercises the shipping
# matcher rather than a copy of it.
scan() {
    awk '
        {
            raw = $0
            line = $0
            sub(/\/\/.*$/, "", line)
            stripped = raw
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", stripped)
            is_comment = (stripped ~ /^\/\//)
        }
        {
            # A member DECLARATION: mentions std::thread (but not std::thread::,
            # which is an id or a static), ends in `;`, and has no parenthesis —
            # parens mean a parameter, a construction, or a call.
            mention = (line ~ /std::thread/ && line !~ /std::thread::/)
            is_decl = (mention && line ~ /;[[:space:]]*$/ && line !~ /[()]/)
            if (is_decl && !(raw ~ /joined-by:/) && !block_annotated) {
                printf "  %s:%d: %s\n", FILENAME, FNR, raw
            }
            # Preceding-comment-block state, maintained AFTER the check so a
            # declaration never counts as its own annotation block; any
            # non-comment line (blank included) breaks contiguity.
            if (is_comment) {
                if (raw ~ /joined-by:/) block_annotated = 1
            } else {
                block_annotated = 0
            }
        }
    ' "$1"
}

# --------------------------------------------------------------------------
# SELF-TEST — must-catch/must-exempt cases against the same matcher.
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.h"
    st_fail=0

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

    expect yes "plain member"       '    std::thread m_worker;'
    expect yes "vector of threads"  '    std::vector<std::thread> m_pool;'
    expect yes "unrelated comment"  '    // background worker' '    std::thread m_worker;'
    expect yes "blank breaks block" '    // joined-by: shutdown()' '' '    std::thread m_worker;'
    expect no  "annotated same line" '    std::thread m_worker;  // joined-by: shutdown() (PluginManager::shutdown)'
    expect no  "annotated block"    '    // joined-by: stop(), called from the' \
                                    '    // orchestrated Shutdown chain.' \
                                    '    std::thread m_worker;'
    expect no  "function parameter" 'inline void spinThenDetach(std::thread& thread,'
    expect no  "construction"       '    m_thread = std::thread([this]() { run(); });'
    expect no  "thread id member"   '    std::thread::id m_workerId;'
    expect no  "comment mention"    '    // the std::thread m_worker below is joined in stop()'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "Thread join-site lint SELF-TEST FAILED."
        exit 1
    fi
    echo "Thread join-site lint self-test clean (10 cases)."
    exit 0
fi

fail=0
checked=0

while IFS= read -r f; do
    checked=$((checked + 1))
    out=$(scan "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done < <(find mxbmrp3 -path mxbmrp3/vendor -prune -o \( -name '*.cpp' -o -name '*.h' \) -print | sort)

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

THREAD JOIN-SITE LINT FAILED.
Each line above declares a std::thread member without a joined-by annotation.
A destructor join at DLL detach deadlocks under the loader lock, and the game
does unload without calling Shutdown() — so the thread must be joined by the
orchestrated Shutdown() chain, with the destructor at most a spinThenDetach
backstop (see CLAUDE.md Maintenance Invariants and thread_detach_grace.h).
Name the join site:

    std::thread m_worker;  // joined-by: shutdown() (PluginManager::shutdown)
EOF
    exit 1
fi
echo "Thread join sites clean (${checked} sources checked)."
