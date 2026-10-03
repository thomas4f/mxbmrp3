#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_thread_safety.sh
# The raw-std::mutex lint: synchronization must use the annotated wrappers.
#
# THE INVARIANT (CLAUDE.md): a mutex-guarded member is guarded at EVERY access
# site. Clang's thread-safety analysis enforces that for annotated members, and
# it runs inside check_clang_tidy.sh (same TUs, same flags, one parse instead
# of the two this gate and that one used to spend). What the analysis cannot do
# is see a raw std::mutex / std::lock_guard / std::unique_lock: those carry no
# annotations, so new synchronization built on them silently dodges every check.
# This grep is the other half - no compiler, so it runs in the fast label.
#
# thread_safety.h itself (the wrappers' implementation) is the only legitimate
# site; anything else needs a `// tsa-exempt: <reason>` annotation on the line.
#
#   ./tests/integration/check_thread_safety.sh
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "${HERE}/../../mxbmrp3" && pwd)"

echo "==> raw std::mutex lint (unannotated sync primitives dodge the analysis)"
raw=$(grep -rn "std::mutex\|std::lock_guard\|std::unique_lock\|std::recursive_mutex" \
        "${SRC}/core" "${SRC}/hud" "${SRC}/handlers" "${SRC}/diagnostics" \
        --include='*.cpp' --include='*.h' \
      | grep -v "thread_safety.h:" | grep -v "tsa-exempt:" | grep -v "^\s*//" \
      | grep -vE ':[0-9]+:\s*//')
if [ -n "${raw}" ]; then
    echo "${raw}" >&2
    echo "THREAD-SAFETY: raw std sync primitive outside thread_safety.h (use Mutex/MutexLock/CvLock, or annotate '// tsa-exempt: <reason>')" >&2
    exit 1
fi
echo "No raw std sync primitives."
