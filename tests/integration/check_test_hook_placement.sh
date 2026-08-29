#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_test_hook_placement.sh
# Test-hook placement lint (no compiler needed, pure grep/awk).
#
# THE INVARIANT (CLAUDE.md "Maintenance Invariants" / "Non-obvious placements"):
# every MXBMRP3_Test_* hook lives in core/test_hooks.cpp, which is (a) gated on
# MXBMRP3_TEST_BUILD and (b) removed from the source list of every shipping
# target by mxbmrp3/CMakeLists.txt — the two fences that keep test exports out
# of a released DLL. A hook defined in any other file has neither fence by
# default: it compiles into the shipping targets and SHIPS, silently. Nothing
# caught that before this lint; the convention was review-only.
#
# So: any code (non-comment) mention of MXBMRP3_Test_ outside core/test_hooks.cpp
# fails, with two deliberate exemptions:
#   - a `friend` declaration (performance_hud.h grants a hook access to
#     privates; a declaration exports nothing and defines nothing);
#   - a `// test-hook-exempt: <reason>` annotation on the line or the line
#     above (same escape-hatch shape as api-guard-exempt / vis-gate).
# Comment-only mentions ("see MXBMRP3_Test_Foo") are stripped before matching.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <file> -> offending lines (empty output == clean). Both the real run and
# --self-test go through this, so the self-test exercises the shipping matcher.
scan() {
    awk '
        {
            raw = $0
            line = $0
            sub(/\/\/.*$/, "", line)
            if (raw ~ /test-hook-exempt:/) pending = 2
            if (line !~ /MXBMRP3_Test_/) { if (pending) pending--; next }
            annotated = pending
            if (pending) pending--
            if (line ~ /(^|[[:space:]])friend[[:space:]]/) next
            if (annotated) next
            printf "  %s:%d: %s\n", FILENAME, FNR, raw
        }
    ' "$1"
}

# --------------------------------------------------------------------------
# SELF-TEST — must-catch/must-exempt cases against the same matcher.
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.cpp"
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

    expect yes "exported definition" '__declspec(dllexport) int MXBMRP3_Test_Foo(void) {'
    expect yes "plain definition"    'void MXBMRP3_Test_Foo(int x) {'
    expect yes "call site"           '    MXBMRP3_Test_Foo(1);'
    expect yes "guarded definition"  '#ifdef MXBMRP3_TEST_BUILD' \
                                     'int MXBMRP3_Test_Foo(void) { return 0; }' '#endif'
    expect no  "comment mention"     '    // see MXBMRP3_Test_Foo for the harness side'
    expect no  "friend declaration"  '    friend void MXBMRP3_Test_FooImpl(unsigned int);'
    expect no  "exempt same line"    'MXBMRP3_Test_Foo(1);  // test-hook-exempt: reason'
    expect no  "exempt line above"   '// test-hook-exempt: reason' 'MXBMRP3_Test_Foo(1);'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "Test-hook placement lint SELF-TEST FAILED."
        exit 1
    fi
    echo "Test-hook placement lint self-test clean (8 cases)."
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
done < <(find mxbmrp3 -name '*.cpp' -o -name '*.h' | grep -v '^mxbmrp3/core/test_hooks.cpp$' | sort)

if [[ $fail -ne 0 ]]; then
    cat <<'EOF'

TEST-HOOK PLACEMENT LINT FAILED.
Each line above references MXBMRP3_Test_ in code outside core/test_hooks.cpp.
Hooks belong in test_hooks.cpp — the one file that is both gated on
MXBMRP3_TEST_BUILD and excluded from every shipping target, so a hook there
cannot reach a released DLL. A hook anywhere else ships. Move the definition;
if a non-hook reference is genuinely needed (a friend declaration already is
exempt), annotate it:

    // test-hook-exempt: <why this cannot live in test_hooks.cpp>
EOF
    exit 1
fi
echo "Test-hook placement clean (${checked} sources checked)."
