# Development & Testing

How to build and test MXBMRP3. This is the home for build/test/dev workflow;
[`README.md`](README.md) is user-facing, [`ARCHITECTURE.md`](ARCHITECTURE.md)
is the technical deep-dive, and [`CLAUDE.md`](CLAUDE.md) is the AI-assistant
context.

There are **two build tracks**:

| | Shipping build | Cross-build (test) |
|---|---|---|
| Toolchain | MSVC (Visual Studio 2022) | mingw-w64 on Linux |
| Output | the real `.dlo` users install | a functional test DLL |
| Runs | in-game on Windows | headless under Wine / CI |
| Purpose | releases | fast build + test on any host, no game |

The shipping `.dlo` is produced **only** by MSVC. The cross-build is a
portability configuration used purely for automated testing — it is not a
shippable artifact (analytics/Discord are compiled out, SEH crash-handling is
MSVC-only, no analytics keys). Both build from the same source; all divergences
are gated by `MXBMRP3_TEST_BUILD` / `_MSC_VER`, so the MSVC build is unaffected.

---

## Shipping build (Windows / MSVC)

- **Open** `mxbmrp3.sln` in Visual Studio 2022 (C++17, v143 toolset).
- **Platform**: x64 only (all PiBoSo games are 64-bit).
- **Configurations**:
  - `All-Release` / `All-Debug` → builds MXB + GPB + KRP via the `build_all`
    meta-project (default in the dropdown).
  - `MXB-Release` → `build/MXB-Release/mxbmrp3.dlo`
  - `GPB-Release` → `build/GPB-Release/mxbmrp3_gpb.dlo`
  - `KRP-Release` → `build/KRP-Release/mxbmrp3_krp.dlo`
- **Deploy**: copy the `.dlo` to the game's `plugins/` folder.
- **Debug**: use a Debug configuration (enables `DEBUG_INFO` macros).
- **Analytics keys (release only)**: a `Release` (`NDEBUG`) build hard-fails
  unless the two secrets `APTABASE_KEY` / `GOATCOUNTER_TOKEN` are set (or
  `MXBMRP3_ALLOW_NO_ANALYTICS` is defined). The GoatCounter *code* is public and
  hardcoded, so it's not required. Plain dev/Debug builds need nothing.

The game engine doesn't support C++ exceptions in the render/telemetry path and
runs at up to 240fps — see [`ARCHITECTURE.md`](ARCHITECTURE.md) and
[`CLAUDE.md`](CLAUDE.md) for the invariants that follow from that.

---

## Testing (Linux / cross-build)

**Full guide: [`TESTING.md`](TESTING.md)** — the five layers, the harness, the
testing philosophy, and how to add a test. Everything runs on any Linux host and
in CI (`.github/workflows/tests.yml`), no game and no Windows (in this private
repo the CI jobs are gated to manual/release runs; the public mirror runs them on
every push). The
essentials:

```bash
./tests/unit/run_tests.sh          # Layer 1: pure-logic unit tests (doctest, ~1s, just g++)
./tests/integration/build.sh         # cross-compile the plugin -> Windows DLL (mingw, incremental)
./tests/integration/run_tests.sh     # Layer 2: doctest integration tests under Wine (real callbacks)
./tests/integration/run_persist_test.sh   # Layer 3: settings round-trip (and run_fuzz / run_perf / ...)
./tests/web/run.sh            # Layer 4: Playwright overlay tests (?demo, headless Chromium, Node.js)
./tests/asan/run.sh           # Layer 5: AddressSanitizer sweep (see TESTING.md)
```

Layer 1 covers the header-only pure logic in `core/plugin_utils.h`. Layer 2
cross-compiles the real plugin, drives the real PiBoSo callbacks under Wine, and
asserts the plugin's own computed state (`snapshot()`, or typed `MXBMRP3_Test_*`
hooks for internal state) — including **real-data golden masters** that replay
captured in-game callback tapes; it catches logic regressions, not just
portability breakage. Layer 3 is the specialized modalities (persistence property
test, config/callback fuzzing, CPU perf baseline). Layer 4 drives the web overlay
in a real browser. The cross-build's source divergences from the shipping MSVC
build are in [`tests/integration/README.md`](tests/integration/README.md).

### Prerequisites (Linux)

```bash
sudo apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 ccache wine64 python3
# std::thread/std::mutex require the posix threading variant:
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
```

Unit tests need only `g++`. The integration and specialized tests additionally
need `wine64` (and `python3` for the fuzz/persist runners). The Layer 4 web
tests need only Node.js — `tests/web/run.sh` installs Playwright + Chromium on
first run.

---

## Git & versioning

- **Branches**: `claude/descriptive-name-sessionID` for AI-assisted work.
- **Version**: edit only `VER_MAJOR/MINOR/PATCH` in `mxbmrp3/resource.h`; the
  4th component is stamped at build time from the git commit count. The version
  isn't kept in git tags — but cutting a release creates a `vX.Y.Z` tag (see
  below), derived from `resource.h`. See [`CLAUDE.md`](CLAUDE.md) →
  "Version Management".

---

## Releases

The `release` workflow ([`.github/workflows/release.yml`](.github/workflows/release.yml))
builds all three games with MSVC on a Windows runner and packages the zip + NSIS
installer + debug symbols + SBOM (generated by
[`tools/gen_sbom.py`](tools/gen_sbom.py) from
[`mxbmrp3/vendor/vendored.json`](mxbmrp3/vendor/vendored.json)). It runs the **full test suite
first** (via the reusable `tests.yml`) and `build-release needs: tests`, so nothing
ships from a red commit.

- **Cut a release (recommended)**: Actions → **release** → **Run workflow**, tick
  **"Create draft release"**. It auto-creates the `vX.Y.Z` tag (from `resource.h`)
  at that commit and opens a **draft** GitHub Release with every asset attached
  and the passing test run linked — review, then **Publish**. It fails fast if
  that `vX.Y.Z` tag already exists (bump `resource.h` first).
- **Dry run**: same, box **unticked** — builds and uploads the artifacts (zip,
  installer, symbols, SBOM), no tag, no Release. Use it to validate.

  The button is the **only** release trigger — there is no tag-push path. The
  `vX.Y.Z` tag is created *by* the release (from `resource.h`), so it can never
  drift from the version and you never hand-create tags.
- **Locally**: [`packaging/make_release.bat`](packaging/make_release.bat) does the
  same MSVC build + packaging into `dist\` (run it from anywhere — it cd's to the
  repo root). It shares the release-notes template
  ([`packaging/release_readme.txt`](packaging/release_readme.txt)) and SBOM generator
  ([`tools/gen_sbom.py`](tools/gen_sbom.py)) with the workflow, so local and CI
  output match.

**Debug symbols** (`mxbmrp3-symbols-vX.Y.Z.zip`, `.pdb` + linker `.map` for all
three games) are archived with every release — keep them; they're what resolve a
crash dashboard's `mxbmrp3.dlo+0xNNNN` offset to a function months later.

### Publishing to the public mirror

Development happens in the private repo; users download from the public repo
(`thomas4f/mxbmrp3`), which gets **one squashed commit per release** so the
private iteration history is never exposed. The `mirror` workflow
([`.github/workflows/mirror.yml`](.github/workflows/mirror.yml)) automates that
copy — it creates a single commit whose tree is identical to private `main`,
parented on `public/main` (`git commit-tree`), and pushes it. No working-tree
copy, so it can't half-apply.

The end-to-end release flow (all buttons, no local git):

1. Bump `VER_*` in `mxbmrp3/resource.h`; merge everything into private `main`.
2. **Private** → Actions → **release**, box **unticked** (dry run). Confirm green
   and inspect the built zip/installer artifacts.
3. **Private** → Actions → **mirror**, box **unticked** — preview
   `git diff --stat public/main..HEAD` (the "verify before push" step).
4. **Private** → Actions → **mirror**, box **ticked** — push the squashed tree
   to `public/main`.
5. **Public** → Actions → **release**, box **ticked** — build/test/tag/draft the
   Release *where users download*. Building on the public tree means the
   FILEVERSION 4th component (`git rev-list --count HEAD`) matches a rebuild from
   public source. Review, then **Publish**.

The `mirror` step is **source-only** — it does not run the test suite, so it can
push `main`'s tree to public before any CI on that exact commit finishes. The
release gate lives downstream in step 5 (`release` → `build-release needs: tests`),
so a *binary* never ships red. Doing the dry-run **release** in step 2 first is
therefore deliberate: it confirms `main` is green and buildable *before* you
mirror, so the public source and the shipped binary stay in lockstep. `mirror`
also refuses to run from any ref other than the default branch.

**One-time setup**: the public repo needs the `APTABASE_KEY` / `GOATCOUNTER_TOKEN`
Actions Secrets (a Release build hard-errors without them); the private repo needs
a `PUBLIC_REPO_TOKEN` secret — a fine-grained PAT with `contents: write` on
`thomas4f/mxbmrp3` — for the mirror push. `packaging/make_release.bat` stays as the
offline/local build path; it shares the template + SBOM generator with the
workflow, so its output matches CI.
