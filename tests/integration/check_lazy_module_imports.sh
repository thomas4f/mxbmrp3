#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_lazy_module_imports.sh
# Import-table lint for the libraries this plugin loads DYNAMICALLY on purpose.
#
# THE INVARIANT: the plugin gains no import-table entry for opengl32, d3d11,
# d3dcompiler_47 or dcomp. All four are resolved at RUNTIME - GetModuleHandle
# or LoadLibrary plus GetProcAddress - and the reason is stated in
# core/hud_gpu_renderer.h and core/gl_probe.h: an import is resolved by the
# loader when the host maps the DLL, so a machine without the library, or with
# an older one, fails to load the PLUGIN rather than falling back. The whole
# fallback design (GPU -> software -> engine rendering; GL probe -> silent
# no-op) depends on there being nothing for the loader to fail on.
#
# opengl32 carries an extra trap the others do not: unlike d3d11, it IS present
# on every Windows machine, so an accidental import would never fail to load
# and would never be noticed - it would just quietly make the plugin a GL
# client on every launch, including for the players who never enable any of
# this. That is exactly the class of mistake a lint catches and review does not.
#
# It also catches the likeliest way in, which is not writing an import
# deliberately: #include <GL/gl.h> (or <d3d11.h>) and calling one function
# directly rather than through the resolved pointer. Nothing else in the tree
# notices that - it compiles, links, and works on the developer's machine.
#
# WHY BESPOKE (CLAUDE.md: prefer the off-the-shelf tool): this is a property of
# a LINKED ARTIFACT, not of source, so no source linter can see it. objdump
# ships with the mingw toolchain the cross-build already requires, and the
# whole check is one grep over its output - there is no tool to adopt here,
# only a tool to use.
#
#   ./tests/integration/check_lazy_module_imports.sh
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

DLL="tests/integration/build/mxbmrp3_test.dlo"
OBJDUMP="${OBJDUMP:-x86_64-w64-mingw32-objdump}"

if ! command -v "${OBJDUMP}" >/dev/null; then
    echo "SKIP: ${OBJDUMP} not found. Install with: ./tools/install_deps.sh mingw" >&2
    exit 3
fi
# Build if needed rather than skipping: CTest may run this before the
# cross-build gate under -j, and a SKIP there would read as "checked, nothing
# to report" when nothing was checked at all. build.sh is incremental, so this
# is a no-op on an up-to-date tree - the same thing run_tests.sh and the other
# slow gates already do.
if [ ! -f "${DLL}" ]; then
    ./tests/integration/build.sh >/dev/null || {
        echo "FAIL: could not build ${DLL} to inspect its imports" >&2; exit 1; }
fi

# The libraries that must never appear. Matched case-insensitively: the import
# table preserves whatever case the linker recorded.
LAZY="opengl32 d3d11 d3dcompiler dcomp"

imports=$("${OBJDUMP}" -p "${DLL}" | sed -n 's/^\s*DLL Name: *//p')
# A scan that matched nothing would pass vacuously - the failure mode every
# source-scanning gate in this repo guards against. A DLL with no imports at
# all is not a DLL we built.
count=$(printf '%s\n' "${imports}" | grep -c . || true)
if [ "${count}" -lt 5 ]; then
    echo "FAIL: only ${count} imports read from ${DLL} - objdump output not understood," >&2
    echo "      so this check would pass no matter what. Not a pass." >&2
    exit 1
fi

fail=0
for lib in ${LAZY}; do
    if printf '%s\n' "${imports}" | grep -qi "^${lib}\.dll$"; then
        echo "FAIL: ${DLL} imports ${lib}.dll."
        echo "      It must be resolved at RUNTIME instead (GetModuleHandle/LoadLibrary +"
        echo "      GetProcAddress), so a machine without it falls back rather than failing"
        echo "      to load the plugin at all. The usual cause is including the library's"
        echo "      header and calling a function directly instead of through the resolved"
        echo "      pointer. See core/hud_gpu_renderer.h and core/gl_probe.h."
        fail=1
    fi
done

if [ ${fail} -ne 0 ]; then exit 1; fi
echo "lazy module imports OK: ${count} imports, none of: ${LAZY}"
