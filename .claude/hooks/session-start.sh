#!/bin/bash
# ============================================================================
# SessionStart hook — provision the headless test toolchain for Claude Code on
# the web, so a session can build + run the tests immediately.
#
# The shippable .dlo is MSVC-only (Windows). On Linux the tests cross-compile the
# plugin to a Windows DLL with mingw-w64 and run it under wine64:
#   ./tests/unit/run_tests.sh          -> pure-logic unit tests (needs only g++)
#   ./tests/integration/run_tests.sh   -> mingw cross-build + Wine integration
# Idempotent: the container is cached after the first run, so later sessions skip
# the install and just re-export the env.
# ============================================================================
set -euo pipefail

# Web (remote) sessions only — a local machine already has its own toolchain.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

LOG=/tmp/mxbmrp3-toolchain.log

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 \
   || ! command -v wine64 >/dev/null 2>&1 \
   || ! command -v ccache  >/dev/null 2>&1 \
   || ! command -v makensis >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  # Install wine64 WITHOUT the 32-bit packages: wine32:i386 has broken i386 deps
  # in this image, and the tests run WINEARCH=win64 anyway. nsis (official) is for
  # .nsi work; the tests themselves don't need it.
  if ! { sudo apt-get update -qq \
         && sudo apt-get install -y --no-install-recommends \
              gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 ccache wine64 nsis python3; \
       } >"$LOG" 2>&1; then
    echo "session-start: toolchain install failed"; tail -25 "$LOG"; exit 1
  fi
  # std::thread / std::mutex require the posix threading variant of mingw.
  sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 || true
  sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix >/dev/null 2>&1 || true
fi

# Wine launcher fix: the packaged /usr/bin/wine resolves its loader from argv[0]
# and fails with "could not exec the wine loader". Replace it with a wrapper that
# execs the real loader by absolute path.
if [ -x /usr/lib/wine/wine64 ] && ! head -1 /usr/bin/wine 2>/dev/null | grep -q '^#!/bin/sh'; then
  printf '#!/bin/sh\nexec /usr/lib/wine/wine64 "$@"\n' | sudo tee /usr/bin/wine >/dev/null
  sudo chmod +x /usr/bin/wine
fi

# Persist the Wine env for the whole session (WINELOADER is the load-bearing one).
{
  echo 'export WINELOADER=/usr/lib/wine/wine64'
  echo 'export WINEARCH=win64'
  echo 'export WINEDEBUG=-all'
  echo "export WINEPREFIX=\"\${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}\""
} >> "$CLAUDE_ENV_FILE"

# Warm the Wine prefix once so the first integration test isn't racing its init.
export WINELOADER=/usr/lib/wine/wine64 WINEARCH=win64 WINEDEBUG=-all
export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
if [ ! -d "$WINEPREFIX" ]; then
  wineboot -i >/dev/null 2>&1 || true
  wineserver -w >/dev/null 2>&1 || true
fi

echo "session-start: toolchain ready ($(x86_64-w64-mingw32-g++ --version | head -1 | awk '{print $1,$NF}'); $(wine --version 2>/dev/null || echo 'wine unknown'))"
