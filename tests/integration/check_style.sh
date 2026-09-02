#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_style.sh
# File-hygiene lint over every hand-authored source file (no compiler needed).
#
# THE RULES (mirrored in .editorconfig, so a conforming editor applies them as
# you type and this check catches the editors that don't):
#   1. no tab characters          — indentation is spaces everywhere
#   2. no trailing whitespace     — except .md, where a two-space line ending
#                                   is a hard line break
#   3. no CRLF line endings       — the repo is LF-only (.gitattributes
#                                   normalizes, this catches what slips past)
#   4. a final newline            — a missing one makes the next diff touch an
#                                   unrelated line
#
# WHY THIS AND NOT clang-format. The obvious move is a formatter, and it was
# evaluated properly before being rejected: 18 candidate configurations were
# measured against the tree, and the CLOSEST achievable one (Google base,
# 4-space indent, DontAlign continuations, no column limit) still rewrote
# 12,545 of 86,252 non-vendor lines — 14.5%. What it rewrites is not noise:
# this codebase aligns trailing comments into columns, wraps long argument
# lists at meaningful boundaries, and formats banner comments by hand, and all
# of that is load-bearing for readability. Adopting a formatter would trade a
# real, deliberate style for a mechanical one and churn every future diff.
# So layout stays a review concern, and this check enforces only the part that
# has a single correct answer.
#
# ZERO CHURN BY CONSTRUCTION. Every rule above already held across all scanned
# files when this check was written — it protects a property the project
# already has, rather than demanding a cleanup. If it fails, something new
# regressed.
#
#   ./tests/integration/check_style.sh
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# Tracked, hand-authored files only. Excluded, in order of the pathspec:
#   vendor/**              third-party source, vendored verbatim (see vendored.json)
#   harness/doctest.h      likewise vendored
#   *.sln/*.vcxproj/*.filters
#                          Visual Studio generates and rewrites these on save
#                          (tabs in the .sln, no final newline in the .filters);
#                          the IDE wins that argument, so don't have it
# CMakeLists.txt / *.cmake are in the list because they were NOT, and the gap was
# silent: the build files are as hand-authored as anything here, but matched no
# extension, so a trailing space landed in CMakeLists.txt and this gate passed.
# The header above claims "every hand-authored source file" — that is now true.
mapfile -t FILES < <(git ls-files \
    '*.c' '*.cpp' '*.h' '*.inc' \
    '*.js' '*.css' '*.html' '*.json' '*.yml' '*.yaml' \
    '*.py' '*.sh' '*.md' '*.nsi' '*.cfg' \
    'CMakeLists.txt' '*/CMakeLists.txt' '*.cmake' \
    ':!:mxbmrp3/vendor/**' \
    ':!:tests/integration/harness/doctest.h' \
    ':!:*.sln' ':!:*.vcxproj' ':!:*.filters')

fail=0
report() {   # report <rule> <file> [detail]
    printf '  %s: %s%s\n' "$1" "$2" "${3:+ ($3)}"
    fail=1
}

# ONE PROCESS PER RULE, not four per file. The obvious per-file loop forked
# ~3,000 greps over 742 files: 6 s idle, but starved under `ctest -j` alongside
# the Wine gates and blew the 120 s gate timeout. `grep -c` over the whole list
# prints `file:count` per file, so each rule is a single scan of the tree.
# Empty files (.gitkeep and friends) are excluded up front rather than skipped
# in a loop, and the final-newline rule reads one byte per file in one perl.
mapfile -t FILES < <(for f in "${FILES[@]}"; do [ -s "$f" ] && echo "$f"; done)

scan() {   # scan <rule> <pcre> <files...>: report every file with >0 matches
    local rule=$1 pat=$2; shift 2
    [ $# -gt 0 ] || return 0
    while IFS=: read -r f n; do
        if [ "$n" -gt 0 ]; then report "$rule" "$f" "$n line(s)"; fi
    done < <(grep -cP "$pat" "$@" || true)
    return 0    # under set -e a trailing false test would abort the script
}

scan "tab character" '\t' "${FILES[@]}"
scan "CRLF line ending" '\r$' "${FILES[@]}"
# Hard line breaks in Markdown are a trailing double-space, so the rule
# genuinely does not apply there.
mapfile -t NON_MD < <(printf '%s\n' "${FILES[@]}" | grep -v '\.md$' || true)
scan "trailing whitespace" '[ \t]+$' "${NON_MD[@]}"
while IFS= read -r f; do
    report "missing final newline" "$f"
done < <(perl -e 'for (@ARGV) { open(my $h, "<", $_) or next; seek($h, -1, 2);
                   read($h, my $c, 1); print "$_\n" if $c ne "\n"; }' "${FILES[@]}")

if [ "$fail" -ne 0 ]; then
    cat <<'EOF'

STYLE LINT FAILED.
The rules above are mechanical (tabs / trailing whitespace / CRLF / final
newline) and are also in .editorconfig — an editor with EditorConfig support
fixes them on save. This check exists for the editors that don't.

To fix by hand:
  sed -i 's/[ \t]*$//' <file>      # trailing whitespace
  sed -i 's/\t/    /g'  <file>     # tabs -> 4 spaces
  printf '\n' >> <file>            # missing final newline

Layout (wrapping, comment alignment) is deliberately NOT checked here — see
this script's header for why clang-format is not used.
EOF
    exit 1
fi

echo "Style clean (${#FILES[@]} files: no tabs, no trailing whitespace, LF only, final newline present)."
