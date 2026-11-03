#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_name_literals.sh
# Folder-name lint for plugin source (no compiler needed, pure awk).
#
# THE INVARIANT: the two folders the plugin owns on a player's disk --
# Documents\PiBoSo\<Game>\mxbmrp3\ and <Game>\plugins\mxbmrp3_data\ -- are
# spelled once, in core/plugin_names.h (MXB_USER_DIR_NAME, MXB_DATA_DIR_NAME).
# Before that header they were spelled out in 27 files, eight of which each
# declared their own `*_SUBDIRECTORY = "mxbmrp3"`; one that drifted would write
# a player's data somewhere the rest of the plugin never reads.
#
# WHAT IS FLAGGED: a string literal, outside comments, that uses `mxbmrp3` or
# `mxbmrp3_data` as a whole PATH SEGMENT (the literal is exactly the name, or
# the name sits next to a `\` or `/`) and contains no whitespace.
#
# What is NOT flagged, by design:
#   - Prose (any literal with a space): toasts, notices, log lines, INI comments
#     that mention the folder. Player-visible prose is owned by the translation
#     work (core/i18n.h), and the name stays literal in it -- it is not
#     translated. Rewriting prose around a macro would only make it unreadable.
#   - File names such as "mxbmrp3_settings.ini": `mxbmrp3_settings` is not the
#     folder, and the whole name reads (and greps) better as one literal.
#   - The display name "MXBMRP3" (the match is case-sensitive).
#   - URLs (any literal containing "://"): external addresses, fixed elsewhere.
#   - #include lines (the source folder is also called mxbmrp3).
#
# A literal that genuinely must stay (an external id that happens to equal the
# folder name) carries `// name-literal: <reason>` on the same line or the line
# above -- the annotation shape check_visibility_gates.sh uses for `vis-gate:`.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

# scan <file...> -> offending lines (empty output == clean). Both the real run
# and --self-test go through this, so the self-test exercises the shipping
# matcher rather than a copy of it.
scan() {
    awk '
        FNR == 1 { inblk = 0; prev = "" }
        {
            raw = $0; s = $0; n = length(s); i = 1; hit = 0
            while (i <= n) {
                c = substr(s, i, 1); d = substr(s, i, 2)
                if (inblk) { if (d == "*/") { inblk = 0; i += 2; continue } i++; continue }
                if (d == "/*") { inblk = 1; i += 2; continue }
                if (d == "//") break
                if (c == "\047") {        # char literal: skip, so a quote char never opens a string
                    i++
                    while (i <= n && substr(s, i, 1) != "\047") { if (substr(s, i, 1) == "\\") i++; i++ }
                    i++; continue
                }
                if (c == "\"") {
                    j = i + 1; lit = ""
                    while (j <= n && substr(s, j, 1) != "\"") {
                        if (substr(s, j, 1) == "\\") { lit = lit substr(s, j, 2); j += 2; continue }
                        lit = lit substr(s, j, 1); j++
                    }
                    if (lit !~ /[ \t]/ && lit !~ /:\/\// &&
                        lit ~ /(^|[\\\/])mxbmrp3(_data)?([\\\/]|$)/) hit = 1
                    i = j + 1; continue
                }
                i++
            }
            if (hit && raw !~ /^[ \t]*#[ \t]*include/ &&
                raw !~ /name-literal:/ && prev !~ /name-literal:/)
                printf "  %s:%d:%s\n", FILENAME, FNR, raw
            prev = raw
        }
    ' "$@"
}

if [[ "${1:-}" == "--self-test" ]]; then
    tmp=$(mktemp -d); trap 'rm -rf "${tmp}"' EXIT
    fixture="${tmp}/fixture.cpp"
    st_fail=0
    cases=0

    # expect <must-be-flagged: yes|no> <label> <line...>
    expect() {
        local want="$1" label="$2"; shift 2
        printf '%s\n' "$@" > "${fixture}"
        local got; got=$(scan "${fixture}" || true)
        local flagged=no; [[ -n "${got}" ]] && flagged=yes
        cases=$((cases + 1))
        if [[ "${flagged}" != "${want}" ]]; then
            echo "  SELF-TEST FAIL [${label}]: expected flagged=${want}, got ${flagged}"
            st_fail=1
        fi
    }

    # Must be caught: every shape a folder path took before plugin_names.h.
    expect yes "subdir constant"    'constexpr const char* X_SUBDIRECTORY = "mxbmrp3";'
    expect yes "appended segment"   '    dir += "mxbmrp3";'
    expect yes "leading backslash"  '    std::string p = savePath + "\\mxbmrp3\\tapes";'
    expect yes "format string"      '    snprintf(b, sizeof(b), "%s\\mxbmrp3\\crashes", root);'
    expect yes "data dir, backslash" '    m_webRoot("plugins\\mxbmrp3_data\\web")'
    expect yes "data dir, slash"    '    f.assetRoot = "plugins/mxbmrp3_data";'
    expect yes "data dir alone"     '    std::string d = pluginDir + "mxbmrp3_data";'
    expect yes "after a char literal" "    char c = '\"'; std::string d = \"mxbmrp3_data/\";"
    expect yes "second literal"     '    f("ok", "plugins\\mxbmrp3_data");'

    # Must NOT be caught.
    expect no  "macro concat"       '    snprintf(b, sizeof(b), "%s\\" MXB_USER_DIR_NAME "\\crashes", root);'
    expect no  "file name"          '    constexpr const char* F = "mxbmrp3_settings.ini";'
    expect no  "file name in path"  '    "%s\\" MXB_USER_DIR_NAME "\\crashes\\mxbmrp3_crash_%lu.dmp"'
    expect no  "prose"              '    toast("Started fresh. A copy is kept in the mxbmrp3 folder");'
    expect no  "prose with path"    '    DEBUG_INFO("Moved mxbmrp3_data/ to backup");'
    expect no  "display name"       '    title = "MXBMRP3";'
    expect no  "URL"                '    open("https://github.com/thomas4f/mxbmrp3");'
    expect no  "include"            '#include "../mxbmrp3/core/x.h"'
    expect no  "line comment"       '    // dir += "mxbmrp3";'
    expect no  "block comment"      '/*' '   dir += "mxbmrp3";' '*/'
    expect no  "annotated same line" '    if (top.find("mxbmrp3")) {  // name-literal: release zip folder'
    expect no  "annotated line above" '    // name-literal: GitHub repo name' '    constexpr const char* R = "mxbmrp3";'

    if [[ ${st_fail} -ne 0 ]]; then
        echo "Folder-name lint SELF-TEST FAILED."
        exit 1
    fi
    echo "Folder-name lint self-test clean (${cases} cases)."
    exit 0
fi

mapfile -t files < <(find mxbmrp3 -path mxbmrp3/vendor -prune -o \
    \( -name '*.h' -o -name '*.cpp' -o -name '*.inc' \) -print | sort |
    grep -v '^mxbmrp3/core/plugin_names\.h$')

out=$(scan "${files[@]}")
if [[ -n "${out}" ]]; then
    echo "${out}"
    cat <<'MSG'

FOLDER-NAME LINT FAILED.
Each line above spells out a folder the plugin owns on the player's disk.
Use the macro from core/plugin_names.h instead -- they concatenate like
literals, so a path stays a compile-time constant:

    "%s\\" MXB_USER_DIR_NAME "\\crashes"      "plugins\\" MXB_DATA_DIR_NAME

If the literal is an external id that merely equals the folder name, say so:

    constexpr const char* REPO = "mxbmrp3";  // name-literal: GitHub repo
MSG
    exit 1
fi
echo "Folder-name literals clean (${#files[@]} files checked)."
