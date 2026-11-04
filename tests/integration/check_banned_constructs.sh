#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_banned_constructs.sh
# Banned-construct lint over first-party plugin source (pure awk, no compiler).
#
# THE RULES (AGENTS.md "DON'T" / "DO"):
#   1. No `throw` of our own. The game engine does not support C++ exceptions,
#      and one escaping an export or a std::thread kills the host. std:: and
#      nlohmann throw, which is why the code is full of try/catch; the rule is
#      that WE never originate one. A bare `throw;` is a rethrow from inside a
#      catch (PluginManager::initialize's rollback hands the original to
#      API_GUARD_CATCH) and is allowed.
#   2. No unbounded C string/format calls: strcpy, strcat, sprintf, vsprintf,
#      wcscpy, wcscat, gets. Use snprintf / strncpy_s / the *_s family.
#   3. No printf / vprintf. Logging goes through DEBUG_INFO_F (the log file);
#      stdout goes nowhere inside the game.
#
# SCOPE. mxbmrp3/** (.cpp/.h/.inc) minus third-party vendor/ - vendor/piboso is
# ours and stays in. Comments and string/char literals (raw strings included)
# are blanked before matching, so prose like "a throw here would..." never
# fires. Code inside `#if defined(MXBMRP3_TEST_BUILD)` / `#ifdef
# MXBMRP3_TEST_BUILD` (and its #elif form) is exempt: it never ships, and test
# hooks inject faults on purpose (PluginThread's testAbortWorker throws).
# Escape hatch for anything else: `// banned-ok: <reason>` on the line.
#
# WHY NOT clang-tidy. Its bugprone-unsafe-functions is the maintained version
# of rule 2, but its list is the C11 Annex K set (it reports nothing for
# strcpy unless __STDC_LIB_EXT1__ is defined, which no toolchain here does) and
# never covers printf; the custom-function list that would fix that is newer
# than the clang-tidy 18 install_deps.sh pins. And no clang-tidy check bans a
# throw expression. All three rules are one token each, and the tidy gate is
# `slow` (minutes) while this runs with the `fast` set in about a second.
#
#   ./tests/integration/check_banned_constructs.sh             # the tree
#   ./tests/integration/check_banned_constructs.sh --self-test # the matcher
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <files...> -> one "  file:line: [rule] source" per violation. Both the
# real run and --self-test go through this, so the self-test exercises the
# shipping matcher rather than a copy of it.
scan() {
    awk '
        BEGIN { SQ = sprintf("%c", 39) }
        FNR == 1 { inblock = 0; inraw = 0; depth = 0; testonly = 0 }

        # strip(s): s with comments and string/char literal CONTENTS blanked.
        # Block-comment and raw-string state carries across lines.
        function strip(s,    out, i, n, c, c2, q, p) {
            out = ""; i = 1; n = length(s)
            while (i <= n) {
                if (inblock) {
                    p = index(substr(s, i), "*/")
                    if (!p) return out
                    i += p + 1; inblock = 0; out = out " "; continue
                }
                if (inraw) {
                    p = index(substr(s, i), rawend)
                    if (!p) return out
                    i += p + length(rawend) - 1; inraw = 0; out = out "\"\""; continue
                }
                c = substr(s, i, 1); c2 = substr(s, i, 2)
                if (c2 == "//") return out
                if (c2 == "/*") { inblock = 1; i += 2; continue }
                if (c == "R" && substr(s, i + 1, 1) == "\"" &&
                    (i == 1 || substr(s, i - 1, 1) !~ /[A-Za-z0-9_]/)) {
                    p = index(substr(s, i + 2), "(")
                    if (p) {
                        rawend = ")" substr(s, i + 2, p - 1) "\""
                        inraw = 1; i += 2 + p; continue
                    }
                }
                # A digit separator (1 then a quote then 000) is not a char literal.
                if (c == SQ && i > 1 && substr(s, i - 1, 1) ~ /[0-9A-Fa-f]/ &&
                    substr(s, i + 1, 1) ~ /[0-9A-Fa-f]/) { out = out c; i++; continue }
                if (c == "\"" || c == SQ) {
                    q = c; i++
                    while (i <= n) {
                        c = substr(s, i, 1)
                        if (c == "\\") { i += 2; continue }
                        i++
                        if (c == q) break
                    }
                    out = out q q; continue
                }
                out = out c; i++
            }
            return out
        }

        {
            raw = $0
            # Preprocessor: track whether we are inside a test-build-only
            # branch. tb[d] = this level opens a test-only branch; the stack
            # keeps nesting honest so an inner #endif cannot end the region.
            if (!inblock && !inraw && raw ~ /^[ \t]*#[ \t]*(if|ifdef|ifndef)([ \t(]|$)/) {
                depth++
                tb[depth] = (raw ~ /^[ \t]*#[ \t]*ifdef[ \t]+MXBMRP3_TEST_BUILD([ \t]|$)/ ||
                             raw ~ /^[ \t]*#[ \t]*if[ \t]+defined[ \t]*\(?[ \t]*MXBMRP3_TEST_BUILD[ \t]*\)?[ \t]*(\/\/.*|\/\*.*)?$/)
                recount(); next
            }
            if (!inblock && !inraw && raw ~ /^[ \t]*#[ \t]*elif/) {
                tb[depth] = (raw ~ /^[ \t]*#[ \t]*elif[ \t]+defined[ \t]*\(?[ \t]*MXBMRP3_TEST_BUILD[ \t]*\)?[ \t]*(\/\/.*|\/\*.*)?$/)
                recount(); next
            }
            if (!inblock && !inraw && raw ~ /^[ \t]*#[ \t]*else/) { tb[depth] = 0; recount(); next }
            if (!inblock && !inraw && raw ~ /^[ \t]*#[ \t]*endif/) {
                if (depth > 0) { delete tb[depth]; depth-- }
                recount(); next
            }

            code = strip(raw)
            if (testonly || raw ~ /banned-ok:/) next

            if (code ~ /(^|[^A-Za-z0-9_])throw([^A-Za-z0-9_]|$)/ && code !~ /(^|[^A-Za-z0-9_])throw[ \t]*;/)
                printf "  %s:%d: [own throw] %s\n", FILENAME, FNR, raw
            if (code ~ /(^|[^A-Za-z0-9_.>])(strcpy|strcat|sprintf|vsprintf|wcscpy|wcscat|gets)[ \t]*\(/)
                printf "  %s:%d: [unbounded C string call] %s\n", FILENAME, FNR, raw
            if (code ~ /(^|[^A-Za-z0-9_.>])v?printf[ \t]*\(/)
                printf "  %s:%d: [printf] %s\n", FILENAME, FNR, raw
        }

        function recount(    k) {
            testonly = 0
            for (k = 1; k <= depth; k++) if (tb[k]) testonly = 1
        }
    ' "$@"
}

# --------------------------------------------------------------------------
# SELF-TEST - must-catch/must-exempt cases against the same matcher.
# --------------------------------------------------------------------------
if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.cpp"
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

    expect yes "own throw"           '    throw std::runtime_error("x");'
    expect yes "throw after code"    '    if (bad) throw 1;'
    expect yes "strcpy"              '    strcpy(dst, src);'
    expect yes "strcat"              '    strcat(dst, src);'
    expect yes "sprintf"             '    sprintf(buf, "%d", n);'
    expect yes "std::sprintf"        '    std::sprintf(buf, "%d", n);'
    expect yes "vsprintf"            '    vsprintf(buf, fmt, ap);'
    expect yes "printf"              '    printf("hello\n");'
    expect yes "std::printf"         '    std::printf("hello\n");'
    expect yes "after a string"      '    log("a // b"); strcpy(d, s);'
    expect yes "after block comment" '    /* note */ throw 1;'
    expect yes "test #else branch"   '#ifdef MXBMRP3_TEST_BUILD' '#else' '    throw 1;' '#endif'
    expect yes "negated test guard"  '#if !defined(MXBMRP3_TEST_BUILD)' '    throw 1;' '#endif'
    expect yes "or'd test guard"     '#if defined(MXBMRP3_TEST_BUILD) || defined(X)' '    throw 1;' '#endif'
    expect yes "after nested endif"  '#ifdef MXBMRP3_TEST_BUILD' '#if X' '#endif' '#endif' '    throw 1;'
    expect yes "after raw string"    '    const char* s = R"(throw()";' '    throw 1;'
    expect no  "rethrow"             '    throw;'
    expect no  "rethrow spaced"      '    } catch (...) { cleanup(); throw ; }'
    expect no  "snprintf"            '    snprintf(buf, sizeof(buf), "%d", n);'
    expect no  "strcpy_s"            '    strcpy_s(dst, sizeof(dst), src);'
    expect no  "strncpy_s"           '    strncpy_s(dst, sizeof(dst), src, _TRUNCATE);'
    expect no  "vsnprintf"           '    vsnprintf(buf, sizeof(buf), fmt, ap);'
    expect no  "DEBUG_INFO_F"        '    DEBUG_INFO_F("x %d", n);'
    expect no  "member printf"       '    w.printf("x");' '    w->printf("x");'
    expect no  "identifier"          '    bool throwable = rethrowCount > 0;'
    expect no  "line comment"        '    // a throw here would strcpy(...) printf("x")'
    expect no  "block comment"       '    /* throw 1;' '       strcpy(a, b); */'
    expect no  "string literal"      '    DEBUG_INFO("would throw; printf(x)");'
    expect no  "escaped quote"       '    log("say \"throw\" now");'
    expect no  "char literal"        "    char c = '\"'; int t = 0;"
    expect yes "digit separator"     "    int a = 1'000; throw 2;"
    expect no  "separator then char" "    int a = 1'000; char q = 'x'; int throwable = 0;"
    expect no  "raw string"          '    const char* s = R"glsl(' '    throw 1; printf("x");' '    )glsl";'
    expect no  "test-build ifdef"    '#ifdef MXBMRP3_TEST_BUILD' '    throw std::runtime_error("x");' '#endif'
    expect no  "test-build defined"  '#if defined(MXBMRP3_TEST_BUILD)' '#if X' '#endif' '    printf("x");' '#endif'
    expect no  "test-build elif"     '#if X' '#elif defined(MXBMRP3_TEST_BUILD)' '    throw 1;' '#endif'
    expect no  "escape hatch"        '    throw 1;  // banned-ok: fixture'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "Banned-construct lint SELF-TEST FAILED."
        exit 1
    fi
    echo "Banned-construct lint self-test clean (${cases} cases)."
    exit 0
fi

mapfile -t FILES < <(find mxbmrp3 -path mxbmrp3/vendor -prune \
    -o \( -name '*.cpp' -o -name '*.h' -o -name '*.inc' \) -print | sort)
mapfile -t PIBOSO < <(find mxbmrp3/vendor/piboso \( -name '*.cpp' -o -name '*.h' \) | sort)
FILES+=("${PIBOSO[@]}")

out=$(scan "${FILES[@]}")
if [[ -n "$out" ]]; then
    echo "$out"
    cat <<'EOF'

BANNED-CONSTRUCT LINT FAILED.
  [own throw]   The game engine does not support C++ exceptions. Report the
                failure (a bool/enum return, a log line); never originate a
                throw. A bare `throw;` rethrow inside a catch is fine.
  [unbounded C string call]
                Use snprintf / strncpy_s / strcat_s (MSVC builds with warnings
                as errors and rejects most of these anyway; the cross-build
                does not).
  [printf]      Use DEBUG_INFO_F - stdout goes nowhere inside the game.
Test-build-only code (#if defined(MXBMRP3_TEST_BUILD)) is exempt. A genuine
exception elsewhere carries `// banned-ok: <reason>` on the line.
EOF
    exit 1
fi
echo "Banned constructs clean (${#FILES[@]} sources: no own throw, no unbounded C string calls, no printf)."
