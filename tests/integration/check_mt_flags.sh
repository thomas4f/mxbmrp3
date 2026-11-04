#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_mt_flags.sh
# Cross-thread member lint for classes that own a thread (pure awk, no compiler).
#
# THE INVARIANT (AGENTS.md "Maintenance Invariants"): a field written off the
# game thread is atomic or mutex-guarded. Background workers legitimately call
# setDataDirty() / SystemMessages::offerUpdate() and set their own run/shutdown
# flags, and a torn or cached plain field there is the classic never-wakes /
# never-stops bug - invisible in review because the declaration looks fine. A
# std::string is worse: a race on one is a heap corruption, not a stale read.
#
# Clang's -Wthread-safety (check_clang_tidy.sh) does NOT cover this: it
# verifies that MUTEX-guarded members are accessed under their mutex, and a
# plain unannotated member is simply outside the analysis. So this check closes
# the other half - every plain value member (bool, the integer and floating
# types, std::string, std::chrono time points/durations) declared in a header
# that mentions std::thread must be one of:
#
#   std::atomic<bool> m_flag;                       - lock-free cross-thread field
#   bool m_flag MXB_GUARDED_BY(m_mutex);            - guarded, and TSA enforces it
#   bool m_flag;  // mt-plain: <why one thread only> - deliberate, with a reason
#
# The annotation is the point: writing the reason forces the author to name the
# thread that owns the field, which is the question that actually matters.
# `mt-plain:` covers ONE declaration: on its line, or in the comment block
# directly above it. A run of fields sharing one owner takes one reason with
# `// mt-plain-block: <reason>`, which covers every declaration below it up to
# the next blank line, access specifier or body brace. The block form is
# spelled differently on purpose: a reason written for one field must not
# silently extend to the fields someone adds after it.
#
# SCOPE. Value members only, in headers that mention std::thread. Containers,
# class-typed members and enums are not matched: an enum member reads exactly
# like a class-typed one to a line matcher, and the containers in these classes
# are either guarded (TSA then checks every access) or worker-local. Windows
# typedefs (DWORD, UINT, HANDLE, ...) and raw pointers are not matched either.
# Statics and constexpr constants are not per-instance state and are skipped.
#
# This used to match `bool` only, while 14 of the `mt-plain:` reasons in the
# tree sat on strings and integers it never looked at - the hatch read as wider
# than the check. It also let a trailing `mt-plain:` on one line excuse the
# NEXT declaration as well (two xinput_reader.h fields were excused that way),
# which the self-test now pins.
#
#   ./tests/integration/check_mt_flags.sh             # the tree
#   ./tests/integration/check_mt_flags.sh --self-test # the matcher
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <file> -> offending declarations (empty output == clean). Both the real
# run and --self-test go through this, so the self-test exercises the shipping
# matcher rather than a copy of it.
scan() {
    awk '
        BEGIN {
            T = "(bool|char|wchar_t|short|int|long|long[ \t]+long|unsigned|unsigned[ \t]+(char|short|int|long|long[ \t]+long)|"
            T = T "float|double|size_t|std::size_t|u?int(8|16|32|64)_t|std::u?int(8|16|32|64)_t|"
            T = T "std::string|std::wstring|std::chrono::[A-Za-z_:]+)"
            DECL = "^[ \t]+(mutable[ \t]+)?" T "[ \t]+m_[A-Za-z_0-9]+"
        }
        FNR == 1 { group = 0; pending = 0 }
        {
            raw = $0
            line = $0
            sub(/\/\/.*/, "", line)   # decide on CODE; the reason lives in the comment

            if (raw ~ /^[ \t]*\/\//) {
                if (raw ~ /mt-plain-block:/) group = 1
                else if (raw ~ /mt-plain:/) pending = 1
                next
            }
            # A plain value data member. Atomics read `std::atomic<...> m_x`,
            # functions carry a parenthesis before the name, and members are
            # the only things at this indent with the m_ prefix.
            if (line ~ DECL) {
                guarded = (line ~ /MXB_GUARDED_BY/)
                excused = (raw ~ /mt-plain:/ || pending || group)
                if (!guarded && !excused)
                    printf "  %s:%d:%s\n", FILENAME, FNR, raw
            }
            # A comment-block `mt-plain:` belongs to the first line of code
            # after it, whatever that line declares.
            pending = 0
            # What separates two owners ends a block: a blank line, an access
            # specifier, or a body opening/closing (a one-line `enum class X {
            # A, B };` or a `{}` initializer does not).
            if (raw ~ /^[ \t]*$/ || line ~ /\{[ \t]*$/ || line ~ /^[ \t]*\}/ ||
                line ~ /^[ \t]*(public|private|protected)[ \t]*:/)
                group = 0
        }
    ' "$1"
}

# --------------------------------------------------------------------------
# SELF-TEST - must-catch/must-exempt cases against the same matcher.
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.h"
    st_fail=0
    cases=0

    expect() {
        local want="$1" label="$2"; shift 2
        cases=$((cases + 1))
        printf '%s\n' "$@" > "${fixture}"
        local got; got=$(scan "${fixture}" || true)
        local flagged=no; [[ -n "${got}" ]] && flagged=yes
        if [[ "${flagged}" != "${want}" ]]; then
            echo "  SELF-TEST FAIL [${label}]: expected flagged=${want}, got ${flagged}"
            st_fail=1
        fi
    }

    expect yes "plain bool"          '    bool m_running = false;'
    expect yes "mutable bool"        '    mutable bool m_cached;'
    expect yes "plain int"           '    int m_count = 0;'
    expect yes "long long"           '    long long m_lastMs = 0;'
    expect yes "unsigned"            '    unsigned m_serial = 0;'
    expect yes "uint32_t"            '    uint32_t m_mask = 0x1F;'
    expect yes "float"               '    float m_speed = 1.0f;'
    expect yes "size_t"              '    size_t m_expected;'
    expect yes "std::string"         '    std::string m_path;'
    expect yes "time point"          '    std::chrono::steady_clock::time_point m_last;'
    expect yes "brace init"          '    std::chrono::steady_clock::time_point m_last{};'
    expect yes "brace ends a block"  '    // mt-plain-block: game thread only' '    int m_a;' '    void f() {' '    }' '    int m_b;'
    expect yes "trailing hatch is per-line" \
                                     '    bool m_a;  // mt-plain: game thread only' \
                                     '    bool m_b;'
    expect yes "comment hatch covers ONE" \
                                     '    // mt-plain: game thread only' '    int m_a;' '    int m_b;'
    expect yes "comment hatch, other type first" \
                                     '    // mt-plain: game thread only' '    Foo m_foo;' '    int m_b;'
    expect yes "blank ends a block"  '    // mt-plain-block: game thread only' '    int m_a;' '' '    int m_b;'
    expect yes "access spec ends a block" \
                                     '    // mt-plain-block: game thread only' '    int m_a;' 'private:' '    int m_b;'
    expect yes "unrelated comment"   '    // the worker reads this' '    int m_a;'
    expect no  "atomic"              '    std::atomic<bool> m_running{false};'
    expect no  "atomic int"          '    std::atomic<int> m_count{0};'
    expect no  "guarded"             '    std::string m_path MXB_GUARDED_BY(m_mutex);'
    expect no  "same-line hatch"     '    int m_a = 0;  // mt-plain: game thread only'
    expect no  "line-above hatch"    '    // mt-plain: game thread only' '    int m_a = 0;'
    expect no  "comment-block hatch" '    // mt-plain: game thread only. Written by' '    // initialize() only.' '    int m_a = 0;'
    expect no  "block hatch"         '    // mt-plain-block: game thread only (all of these)' \
                                     '    float m_a;' '    // per-axis' '    float m_b;' '    bool m_c;'
    expect no  "block past one-line enum" '    // mt-plain-block: game thread only' '    int m_a;' '    enum class E : int { A, B };' '    int m_b;'
    expect no  "block with brace init" '    // mt-plain-block: game thread only' '    int m_a{0};' '    int m_b{0};'
    expect no  "static constexpr"    '    static constexpr int MAX_X = 5;'
    expect no  "static member"       '    static int s_count;'
    expect no  "member function"     '    bool isRunning() const;'
    expect no  "container"           '    std::vector<int> m_items;'
    expect no  "class type"          '    RecordsFetcher m_fetcher;'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "Cross-thread member lint SELF-TEST FAILED."
        exit 1
    fi
    echo "Cross-thread member lint self-test clean (${cases} cases)."
    exit 0
fi

fail=0
files=0
for f in $(grep -rl 'std::thread' mxbmrp3 --include=*.h | grep -v '/vendor/' | sort); do
    files=$((files + 1))
    out=$(scan "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

CROSS-THREAD MEMBER LINT FAILED.
Each field above is a plain value member of a class that owns a std::thread,
with nothing saying which thread owns it. Pick one:

  std::atomic<bool> m_flag;                          # written off the game thread
  std::string m_path MXB_GUARDED_BY(m_mutex);        # guarded (TSA then checks it)
  bool m_flag;  // mt-plain: <which thread, and why that is safe>

A run of fields with one owner can share `// mt-plain-block: <reason>` (covers
down to the next blank line). Name the owning thread in the reason - that is
the question the annotation exists to make you answer.
EOF
    exit 1
fi
echo "Cross-thread members clean (${files} thread-owning headers checked)."
