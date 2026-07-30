#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_mt_flags.sh
# Cross-thread flag lint for classes that own a thread (pure awk, no compiler).
#
# THE INVARIANT (CLAUDE.md "Maintenance Invariants"): a flag written off the
# game thread is `std::atomic<bool>`. Background workers legitimately call
# setDataDirty() / showUpdateNotification() and set their own run/shutdown
# flags, and a torn or cached plain `bool` there is the classic never-wakes /
# never-stops bug — invisible in review because the declaration looks fine.
#
# Clang's -Wthread-safety (check_thread_safety.sh) does NOT cover this: it
# verifies that MUTEX-guarded members are accessed under their mutex, and a
# plain unannotated bool is simply outside the analysis. So this check closes
# the other half — every `bool m_*` member declared in a class that owns a
# std::thread must be one of:
#
#   std::atomic<bool> m_flag;                       — lock-free cross-thread flag
#   bool m_flag MXB_GUARDED_BY(m_mutex);            — guarded, and TSA enforces it
#   bool m_flag;  // mt-plain: <why one thread only> — deliberate, with a reason
#
# The annotation is the point: writing the reason forces the author to name the
# thread that owns the field, which is the question that actually matters.
#
# SCOPE. Only `bool` members, only in headers that mention std::thread. That is
# deliberately narrow — it matches the invariant as written (it is about flags),
# and it keeps the check at zero false positives, which is what stops a lint
# from being ignored. Wider types (counters, timestamps) are covered by review
# and by the mutex annotations.
#
#   ./tests/integration/check_mt_flags.sh
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

fail=0
files=0
for f in $(grep -rl 'std::thread' mxbmrp3 --include=*.h | grep -v '/vendor/' | sort); do
    files=$((files + 1))
    out=$(awk '
        {
            raw = $0
            line = $0
            sub(/\/\/.*/, "", line)   # decide on CODE; the reason lives in the comment

            # A plain bool data member: optional mutable, then `bool m_name`.
            # Anything else (atomic, function returning bool, local, parameter)
            # does not match — atomics read `std::atomic<bool> m_x`, and members
            # are the only things at this indent with the m_ prefix.
            if (line ~ /^[ \t]+(mutable[ \t]+)?bool[ \t]+m_[A-Za-z_0-9]+/) {
                guarded = (line ~ /MXB_GUARDED_BY/)
                # `mt-plain:` may sit on the declaration or the line above it.
                excused = (raw ~ /mt-plain:/ || prev ~ /mt-plain:/)
                if (!guarded && !excused)
                    printf "  %s:%d:%s\n", FILENAME, FNR, raw
            }
            prev = raw
        }
    ' "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

CROSS-THREAD FLAG LINT FAILED.
Each bool above is a plain member of a class that owns a std::thread, with
nothing saying which thread owns it. Pick one:

  std::atomic<bool> m_flag;                          # written off the game thread
  bool m_flag MXB_GUARDED_BY(m_mutex);               # guarded (TSA then checks it)
  bool m_flag;  // mt-plain: <which thread, and why that is safe>

Name the owning thread in the reason — that is the question the annotation
exists to make you answer.
EOF
    exit 1
fi
echo "Cross-thread flags clean (${files} thread-owning headers checked)."
