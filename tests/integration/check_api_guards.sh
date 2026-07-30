#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_api_guards.sh
# DLL-export exception-barrier lint (no compiler needed, pure awk).
#
# THE INVARIANT (CLAUDE.md DO-list): every DLL export in
# mxbmrp3/vendor/piboso/*_api.cpp wraps its body in try { ... }
# API_GUARD_CATCH("Name") — the host game does not support C++ exceptions
# across the plugin API, so an uncaught throw escaping an export terminates
# the GAME process (see vendor/piboso/api_guard.h). This is the plugin's
# single worst failure mode, and nothing enforced the rule for export #33.
#
# This check requires every `__declspec(dllexport)` function in those files
# to contain API_GUARD_CATCH before the next export begins, or to carry an
# explicit exemption on the signature line or the line above:
#
#     // api-guard-exempt: returns a static constant, cannot throw
#     __declspec(dllexport) char* GetModID()
#
# Exemptions are for bodies that trivially cannot throw (return a literal /
# static constant). Anything that calls into plugin code gets a guard.
#
# Scope: vendor/piboso/*_api.cpp only — the game-facing boundary. The
# MXBMRP3_Test_* hooks (core/test_hooks.cpp) are exports too, but exist only
# in the test build where the harness is the caller and a crash is a visible
# test failure, not a player's game going down.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

fail=0
for f in mxbmrp3/vendor/piboso/*_api.cpp; do
    out=$(awk '
        function flush() {
            if (insig && !guarded && !exempt)
                printf "  %s:%d: %s\n", FILENAME, sigline, sig
            insig = 0
        }
        {
            raw = $0
            line = $0
            sub(/\/\/.*/, "", line)   # match on code only: a commented-out
                                       # export must not open a region, and a
                                       # commented-out guard must not satisfy one
            if (line ~ /__declspec\(dllexport\)/) {
                flush()
                insig = 1; guarded = 0
                sig = raw; sigline = FNR
                exempt = (raw ~ /api-guard-exempt:/ || prev ~ /api-guard-exempt:/)
            }
            if (line ~ /API_GUARD_CATCH/ && insig) guarded = 1
            prev = raw
        }
        END { flush() }
    ' "$f")
    if [[ -n "$out" ]]; then
        echo "$out"
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

API-GUARD LINT FAILED.
Each export above has no API_GUARD_CATCH in its body. An uncaught C++
exception crossing the PiBoSo DLL boundary crashes the host game — wrap the
body in try { ... } API_GUARD_CATCH("ExportName") (vendor/piboso/api_guard.h).
If the body trivially cannot throw (returns a literal/static constant), say so
with `// api-guard-exempt: <reason>` on the signature line or the line above.
EOF
    exit 1
fi
echo "API guards clean ($(ls mxbmrp3/vendor/piboso/*_api.cpp | wc -l) API files checked)."
