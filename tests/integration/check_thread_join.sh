#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_thread_join.sh
# Worker-thread join-site lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT (AGENTS.md "Maintenance Invariants"): a worker thread must not
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
# HUD-OWNED WORKERS (the second pass). A HUD's worker has one more ordering
# constraint: HudManager::clear() must join it BEFORE the cached HUD pointers
# are nulled (the slot-clearer loop), because a completing worker reaches other
# HUDs through those pointers - the RecordsHud -> getTimingHud() race. So a
# member of a mxbmrp3/hud/ header whose type owns a std::thread (std::thread
# itself, or a class declared in a header with a std::thread member, e.g.
# RecordsFetcher) must name its joiner with `// joined-by: <fn>()` too, and
# <fn>( must appear in HudManager::clear() above that loop. This is the "join"
# half of AGENTS.md's background-worker rule; the other half (workers snapshot
# their inputs instead of reading live game-thread members) stays review's.
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

# hud_scan <types-regex> <clear-body-file> <hud-header> -> violations. A member
# of a thread-owning type must carry joined-by: <fn>() (line or comment block
# directly above, as scan() reads it), and <fn>( must be in the clear() body.
hud_scan() {
    awk -v types="$1" '
        # The owner as declared: the type itself, or held through a smart
        # pointer / optional (`std::unique_ptr<RecordsFetcher> m_fetcher;`).
        function owner(t) {
            return "((std::(unique_ptr|shared_ptr|optional)<[ \t]*(" t ")[ \t]*>)|(" t "))"
        }
        FNR == NR { sub(/\/\/.*$/, ""); body = body $0 "\n"; next }
        {
            raw = $0
            line = $0
            sub(/\/\/.*$/, "", line)
            stripped = raw
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", stripped)
            is_comment = (stripped ~ /^\/\//)
        }
        !is_comment && line ~ ("^[ \t]+" owner(types) "[ \t]+m_[A-Za-z_0-9]+[ \t]*;") {
            note = (raw ~ /joined-by:/) ? raw : block
            if (note !~ /joined-by:/) {
                printf "  %s:%d: no joined-by: %s\n", FILENAME, FNR, raw
            } else {
                fn = note
                sub(/.*joined-by:[ \t]*/, "", fn)
                sub(/\(.*/, "", fn)
                sub(/.*::/, "", fn)
                # Whole-name match: `Thread(` must not pass on `joinFetchThread(`.
                if (fn !~ /^[A-Za-z_][A-Za-z_0-9]*$/ || body !~ ("(^|[^A-Za-z_0-9])" fn "[ \t]*\\("))
                    printf "  %s:%d: joined-by names %s(), which HudManager::clear() does not call before nulling the HUD pointers: %s\n", FILENAME, FNR, fn, raw
            }
        }
        {
            if (is_comment) block = block " " raw
            else block = ""
        }
    ' "$2" "$3"
}

# clear_body <hud_manager.cpp> -> HudManager::clear() up to the slot-clearer
# loop (the point the HUD pointers are nulled). Non-zero = the anchor moved.
clear_body() {
    awk '
        on && /m_hudSlotClearers\)/ { found = 1; exit }
        on { print }
        # The signature line itself is not body: `joined-by: clear()` would
        # otherwise match it.
        /^void HudManager::clear\(/ { on = 1 }
        END { if (!found) exit 1 }
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

    # HUD-owned workers: the clear() body below joins `joinFetch` only.
    # Cut by the shipping clear_body(), so its signature-line exclusion is
    # exercised too.
    body="${tmp}/clear_body.txt"
    printf '%s\n' 'void HudManager::clear(bool x) {' \
                  '    // stopOther() would be the wrong one' \
                  '    if (m_pRecords) m_pRecords->joinFetch();' \
                  '    for (auto& clearSlot : m_hudSlotClearers) {' > "${tmp}/hm.cpp"
    clear_body "${tmp}/hm.cpp" > "${body}"
    hud_expect() {
        local want="$1" label="$2"; shift 2
        printf '%s\n' "$@" > "${fixture}"
        local got; got=$(hud_scan 'std::thread|Fetcher' "${body}" "${fixture}" || true)
        local flagged=no; [[ -n "${got}" ]] && flagged=yes
        if [[ "${flagged}" != "${want}" ]]; then
            echo "  SELF-TEST FAIL [hud: ${label}]: expected flagged=${want}, got ${flagged}"
            st_fail=1
        fi
    }
    hud_expect yes "unannotated"           '    Fetcher m_fetcher;'
    hud_expect yes "joiner not in clear"   '    Fetcher m_fetcher;  // joined-by: stopFetch()'
    hud_expect yes "joiner only in a comment there" \
                                           '    Fetcher m_fetcher;  // joined-by: stopOther()'
    hud_expect yes "std::thread in a HUD"  '    std::thread m_worker;  // joined-by: stopWorker()'
    hud_expect no  "joined in clear"       '    Fetcher m_fetcher;  // joined-by: joinFetch()'
    hud_expect no  "qualified, in block"   '    // The fetcher is joined first.' \
                                           '    // joined-by: RecordsHud::joinFetch() (HudManager::clear)' \
                                           '    Fetcher m_fetcher;'
    hud_expect no  "other type"            '    FetcherConfig m_cfg;'
    hud_expect yes "unique_ptr owner"      '    std::unique_ptr<Fetcher> m_fetcher;'
    hud_expect yes "optional owner"        '    std::optional<Fetcher> m_fetcher;  // joined-by: stopFetch()'
    hud_expect no  "unique_ptr joined"     '    std::unique_ptr<Fetcher> m_fetcher;  // joined-by: joinFetch()'
    hud_expect yes "suffix of the joiner"  '    Fetcher m_fetcher;  // joined-by: Fetch()'
    hud_expect yes "clear() itself"        '    Fetcher m_fetcher;  // joined-by: HudManager::clear()'

    # The real clear() must still have the anchor the second pass cuts at.
    if ! clear_body mxbmrp3/core/hud_manager.cpp >/dev/null; then
        echo "  SELF-TEST FAIL: HudManager::clear() or its m_hudSlotClearers loop not found in hud_manager.cpp"
        st_fail=1
    fi

    if [[ ${st_fail} -ne 0 ]]; then
        echo "Thread join-site lint SELF-TEST FAILED."
        exit 1
    fi
    echo "Thread join-site lint self-test clean (22 cases + the clear() anchor)."
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

# Second pass: HUD-owned workers are joined in HudManager::clear() before the
# HUD pointers are nulled (see the header). The thread-owning types are the
# top-level classes that declare a std::thread member (the nearest column-0
# class/struct above the member), not every type in their headers.
types='std::thread'
while IFS= read -r cls; do types="${types}|${cls}"; done < <(
    find mxbmrp3 -path mxbmrp3/vendor -prune -o -name '*.h' -print | sort | xargs awk '
        FNR == 1 { cls = "" }
        /^(class|struct)[ \t]+[A-Za-z_][A-Za-z_0-9]*[^;]*$/ {
            cls = $2; sub(/[^A-Za-z_0-9].*/, "", cls)
        }
        /^[ \t]+std::thread[ \t]+m_/ && cls != "" { print cls }
    ' | sort -u)
body=$(mktemp); trap 'rm -f "${body}"' EXIT
if ! clear_body mxbmrp3/core/hud_manager.cpp > "${body}"; then
    echo "  mxbmrp3/core/hud_manager.cpp: HudManager::clear() or its m_hudSlotClearers loop not found - update clear_body() in this script"
    fail=1
fi
for h in mxbmrp3/hud/*.h; do
    out=$(hud_scan "${types}" "${body}" "$h")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done
# Count in awk, not `grep -c`: grep exits 1 on zero matches, and under
# pipefail that would end the script silently the day no HUD owns a worker.
hud_owned=$(awk -v t="${types}" '
    $0 ~ ("^[ \t]+((std::(unique_ptr|shared_ptr|optional)<[ \t]*(" t ")[ \t]*>)|(" t "))[ \t]+m_[A-Za-z_0-9]+[ \t]*;") { n++ }
    END { print n + 0 }' mxbmrp3/hud/*.h)

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

THREAD JOIN-SITE LINT FAILED.
Each line above declares a std::thread member without a joined-by annotation.
A destructor join at DLL detach deadlocks under the loader lock, and the game
does unload without calling Shutdown() — so the thread must be joined by the
orchestrated Shutdown() chain, with the destructor at most a spinThenDetach
backstop (see AGENTS.md Maintenance Invariants and thread_detach_grace.h).
Name the join site:

    std::thread m_worker;  // joined-by: shutdown() (PluginManager::shutdown)

A worker owned by a HUD (mxbmrp3/hud/) is also joined in HudManager::clear()
BEFORE the HUD pointers are nulled, so its joined-by function must be called
there, above the m_hudSlotClearers loop (the model: RecordsHud's
joinFetchThread()).
EOF
    exit 1
fi
echo "Thread join sites clean (${checked} sources checked; ${hud_owned} HUD-owned worker(s) joined in HudManager::clear())."
