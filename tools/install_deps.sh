#!/usr/bin/env bash
# ============================================================================
# tools/install_deps.sh — the ONE list of what the Linux test toolchain needs.
#
# WHY THIS EXISTS. The package list used to live in six places: the CI workflow
# (four separate steps), codeql.yml, the SessionStart hook, and DEVELOPMENT.md's
# prose. Six copies of the same apt line drift, and the drift is invisible until
# a gate skips on a machine somebody swears they provisioned. Now they all call
# this, and the table below is the only thing to edit.
#
#   ./tools/install_deps.sh --list            # show the table, install nothing
#   ./tools/install_deps.sh                   # install everything
#   ./tools/install_deps.sh mingw wine nsis   # install selected groups
#
# TWO LISTS, ONE SUBJECT — keep them straight:
#   * THIS file names PACKAGES (what apt/pip installs).
#   * CMakeLists.txt's mxb_gate TOOLS names BINARIES (what a gate checks for).
# They are not the same string: the `wine64` package ships no `wine64` binary on
# Ubuntu 24.04, and the launcher comes from the separate `wine` package. The
# `provides` column below is the bridge, and tools/check_docs.py fails if a
# binary some gate requires isn't provided by any group here.
#
# Deliberately dumb: a table, apt-get, pip. No skip protocol, no exit-code
# policy, no self-test. If it starts growing those, that is the signal to move
# to a Dockerfile instead (see DEVELOPMENT.md) rather than to grow this.
# ============================================================================
set -euo pipefail

# group | apt packages | pip packages | provides (binaries a gate looks for)
# NOT named GROUPS: that is a bash special variable (the caller's group IDs)
# and assigning to it is silently ignored — --list printed a row of zeros.
DEP_GROUPS=(
    "build|cmake build-essential||cmake g++"
    "python|python3 python3-pip||python3"
    "mingw|gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 ccache||x86_64-w64-mingw32-g++ x86_64-w64-mingw32-objdump"
    # `wine` provides the /usr/bin/wine launcher; `wine64` alone (Ubuntu's 9.0
    # repack) ships only libwine, and the Wine job silently reported "wine not
    # found" for exactly that reason.
    #
    # 64-bit ONLY — no i386 architecture, no wine32:i386. Everything is x86_64:
    # the plugin cross-builds to an x86_64 DLL, the harnesses are x86_64, the NSIS
    # installer is amd64. Beyond slimming the install (libwine:i386 alone is a
    # ~100 MB download every run), dropping the 32-bit side is a CORRECTNESS fix:
    # with wine32 present, Debian's /usr/bin/wine wrapper prefers the 32-bit
    # loader, whose reg.exe reads the WOW6432Node-redirected view and cannot see
    # the amd64 installer's 64-bit registry keys — which silently broke the
    # installer test's HKLM assertions.
    "wine|wine wine64||wine"
    "nsis|nsis||makensis"
    "cppcheck|cppcheck||cppcheck"
    # kramdown is what GitHub Pages runs the README through (Jekyll), which is
    # NOT what github.com renders with - check_pages_render.sh models the site.
    # The GFM parser is a separate package and both are needed; the gate SKIPs
    # rather than failing when either is missing.
    "pages|ruby ruby-kramdown ruby-kramdown-parser-gfm||ruby"
    "clang|clang||clang++"
    "node|nodejs npm||node"
    # Headless screenshots of the REAL companion window
    # (tools/hud_window/companion_demo.sh): Xvfb supplies the display the
    # Win32 window maps into, `import` (ImageMagick) grabs it, `compare` diffs two
    # captures. Missing from this table until it bit: the demo is not a mxb_gate,
    # and check_docs.py only cross-checks binaries a gate declares — so a box
    # provisioned exactly as documented still failed with "'import' not found".
    "screenshot|imagemagick xvfb||import Xvfb"
    # No apt/pip package exists — GitHub ships the CLI + query packs only as a
    # ~1 GB tarball, fetched by the fixup below. Listed here anyway because this
    # table is what check_docs.py cross-checks a gate's TOOLS against, and because
    # "what does the toolchain need" should have exactly one answer.
    "codeql|||codeql"
    "coverage||gcovr|gcovr"
    "lint||ruff|ruff"
    # Defers to tools/requirements.txt rather than repeating pandas/pyarrow:
    # that file is the authority for the TOOLS' runtime deps and carries the
    # version pins and the reasons for them (cairosvg is pinned EXACTLY because
    # the .tga output is only byte-deterministic per renderer version). Naming
    # the packages again here would have been unpinned copies of two of them.
    "analytics||-r tools/requirements.txt|"
)

field() { printf '%s' "$1" | cut -d'|' -f"$2"; }

if [ "${1:-}" = "--list" ]; then
    printf '%-10s %-46s %-16s %s\n' GROUP APT PIP PROVIDES
    for g in "${DEP_GROUPS[@]}"; do
        printf '%-10s %-46s %-16s %s\n' \
            "$(field "$g" 1)" "$(field "$g" 2)" "$(field "$g" 3)" "$(field "$g" 4)"
    done
    exit 0
fi

WANT=("$@")
if [ ${#WANT[@]} -eq 0 ]; then
    for g in "${DEP_GROUPS[@]}"; do WANT+=("$(field "$g" 1)"); done
fi

APT=() ; PIP=() ; CHOSEN=()
for want in "${WANT[@]}"; do
    found=0
    for g in "${DEP_GROUPS[@]}"; do
        [ "$(field "$g" 1)" = "$want" ] || continue
        found=1 ; CHOSEN+=("$want")
        read -ra a <<< "$(field "$g" 2)" ; [ ${#a[@]} -gt 0 ] && APT+=("${a[@]}")
        read -ra p <<< "$(field "$g" 3)" ; [ ${#p[@]} -gt 0 ] && PIP+=("${p[@]}")
    done
    if [ "$found" -eq 0 ]; then
        echo "unknown group '$want' — run --list to see them" >&2
        exit 1
    fi
done

SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO="sudo"

if [ ${#APT[@]} -gt 0 ]; then
    echo "==> apt: ${APT[*]}"
    export DEBIAN_FRONTEND=noninteractive
    $SUDO apt-get update -qq
    $SUDO apt-get install -y --no-install-recommends "${APT[@]}"
fi

if [ ${#PIP[@]} -gt 0 ]; then
    echo "==> pip: ${PIP[*]}"
    python3 -m pip install --quiet "${PIP[@]}"
fi

# --- post-install fixups, only for the groups actually requested ------------
for c in "${CHOSEN[@]}"; do
    case "$c" in
    mingw)
        # std::thread / std::mutex need the POSIX threading variant, which is not
        # the default alternative. Without this the cross-build links but every
        # threaded test deadlocks or fails to start.
        for t in g++ gcc; do
            $SUDO update-alternatives --set "x86_64-w64-mingw32-${t}" \
                "/usr/bin/x86_64-w64-mingw32-${t}-posix" >/dev/null 2>&1 || true
        done
        ;;
    codeql)
        # The bundle (CLI + precompiled query packs) is what the codeql-action
        # uses in CI; the CLI alone would download packs per run. ~1 GB, so it is
        # skipped when already unpacked — this group is opt-in, never part of a
        # default provision on a dev box.
        # Deliberately `releases/latest`, NOT a pinned bundle: this gate is the
        # early-warning copy of what codeql.yml runs, and codeql-action@v4 itself
        # resolves the latest bundle - so pinning here would drift the local query
        # packs AWAY from the authority over time. The cost is that a bundle
        # release can change local findings without a commit; when local and
        # GitHub disagree, GitHub is right.
        if ! command -v codeql >/dev/null 2>&1; then
            CODEQL_HOME="${CODEQL_HOME:-/opt/codeql}"
            echo "==> codeql: fetching bundle into ${CODEQL_HOME} (~1 GB)"
            $SUDO mkdir -p "${CODEQL_HOME}"
            curl -sSL "https://github.com/github/codeql-action/releases/latest/download/codeql-bundle-linux64.tar.gz" \
                | $SUDO tar xz -C "${CODEQL_HOME}" --strip-components=1
            $SUDO ln -sf "${CODEQL_HOME}/codeql" /usr/local/bin/codeql
        fi
        ;;
    wine)
        # Some images ship the loader without a /usr/bin/wine launcher on PATH.
        # Symlink the first one that exists so gates find `wine` rather than
        # skipping — the failure this prevents is a silently green-but-empty run.
        if ! command -v wine >/dev/null 2>&1; then
            for cand in /usr/lib/wine/wine64 /usr/lib/x86_64-linux-gnu/wine/wine64; do
                [ -x "$cand" ] && { $SUDO ln -sf "$cand" /usr/bin/wine; break; }
            done
        fi
        ;;
    esac
done

echo "==> done: ${CHOSEN[*]}"
