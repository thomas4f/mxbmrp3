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
portability configuration used purely for automated testing - it is not a
shippable artifact (analytics/Discord are compiled out, SEH crash-handling is
MSVC-only, no analytics keys). Both build from the same source; all divergences
are gated by `MXBMRP3_TEST_BUILD` / `_MSC_VER`, so the MSVC build is unaffected.

---

## Shipping build (Windows / MSVC)

- **Generate the solution once** (Visual Studio 2022 ships CMake; a Developer
  Command Prompt has it on PATH):
  ```
  cmake --preset msvc
  ```
  The presets live in [`CMakePresets.json`](CMakePresets.json), so the generator
  and architecture are in the repo rather than in your shell history. Visual
  Studio reads that file natively, so Open Folder works too if you prefer it.
  `cmake --list-presets` shows what's available on your host.
- **Open** `build/msvc/mxbmrp3.sln` and build as usual. `ZERO_CHECK` re-runs
  CMake automatically when a `CMakeLists.txt` changes, and the source glob uses
  `CONFIGURE_DEPENDS`, so a new `.cpp` needs no action at all.
- **Platform**: x64 only (all PiBoSo games are 64-bit).
- **Configurations**: plain `Debug` / `Release`. The *game* is the TARGET, not the
  configuration - that is the one habit that changes:
  - `mxbmrp3` → `build/MXB-<Config>/mxbmrp3.dlo`
  - `mxbmrp3_gpb` → `build/GPB-<Config>/mxbmrp3_gpb.dlo`
  - `mxbmrp3_krp` → `build/KRP-<Config>/mxbmrp3_krp.dlo`
  - Ctrl+Shift+B builds all three (what `All-Release` used to do); right-click a
    single project to build just that game - worth doing, since Release LTCG
    intermediates are large. From a shell: `cmake --build --preset mxb`
    (MX Bikes, Release), `mxb-debug`, or `all-games`.
- **Don't edit project settings in the VS properties UI**: the `.vcxproj` files
  under `build/msvc/` are GENERATED and overwritten. Flags, defines and output
  paths live in [`mxbmrp3/CMakeLists.txt`](mxbmrp3/CMakeLists.txt).
- **Deploy**: set `MXB_PLUGIN_PATH` (and/or `GPB_PLUGIN_PATH`, `KRP_PLUGIN_PATH`)
  and the post-build step copies each `.dlo` into that game's `plugins/` folder.
  One variable per game on purpose - a single shared one meant a full build
  dropped all three plugins into whichever game it named, and PiBoSo loads every
  `.dlo` it finds there. Unset variables are simply skipped.
- **Debug**: use the Debug configuration (enables `DEBUG_INFO` macros).
- **Analytics keys (release only)**: a `Release` (`NDEBUG`) build hard-fails
  unless the two secrets `APTABASE_KEY` / `GOATCOUNTER_TOKEN` are set (or
  `MXBMRP3_ALLOW_NO_ANALYTICS` is defined). The GoatCounter *code* is public and
  hardcoded, so it's not required. Plain dev/Debug builds need nothing.
- **The spotter probe is ON in every build**, and there is no longer a switch to
  set: it writes a `SPOTTER SAY [cue] words` line per callout plus the standings
  around it, which is how the spotter's wording and timing get reviewed against a
  recorded tape (`tests/integration/spotter_transcript_driver.cpp`) and, from
  1.29.1 on, how a player's report of the default wording can be read at all.
  Deliberate and temporary - see the block comment at
  `MXBMRP3_SPOTTER_PROBE` in `core/spotter_manager_internal.h` for what it costs and how
  it comes out again.

The game engine doesn't support C++ exceptions in the render/telemetry path and
runs at up to 480fps - see [`ARCHITECTURE.md`](ARCHITECTURE.md) and
[`CLAUDE.md`](CLAUDE.md) for the invariants that follow from that.

---

## Testing (Linux / cross-build)

**Full guide: [`TESTING.md`](TESTING.md)** - the six layers, the harness, the
testing philosophy, and how to add a test. Everything runs on any Linux host and
in CI (`.github/workflows/tests.yml`), no game and no Windows. Neither repo runs
them on a push - you trigger the suite with **Run workflow**, and the release
workflow runs it as its own gate; the free public mirror additionally runs it on
every pull request. The essentials:

**Everything at once, via CTest** (standard runner; `CMakeLists.txt` registers the
gates and builds nothing):

```bash
cmake -S . -B build/tests                            # once
ctest --test-dir build/tests --output-on-failure     # every gate (26 today)
ctest --test-dir build/tests -L fast                 # no mingw/wine needed
```

Each gate declares the executables it needs (`mxb_gate`'s TOOLS argument); a
missing one exits 3, which CTest reports as **SKIPPED** (`SKIP_RETURN_CODE`)
rather than failed - so the suite is useful on a bare box. The unit targets build
with `-Werror`; pass `-DMXB_UNIT_WERROR=OFF` if a newer compiler blocks you.

Or drive the individual layers:

```bash
ctest --test-dir build/tests -R '^unit'   # Layer 1: pure-logic unit tests (doctest, ~1s, just g++)
./tests/unit/coverage.sh 95              # ... + gcov line coverage for that layer (gcovr)
./tests/integration/build.sh         # cross-compile the plugin -> Windows DLL (mingw, incremental)
./tests/integration/run_tests.sh     # Layer 2: doctest integration tests under Wine (real callbacks)
./tests/integration/run_persist_test.sh   # Layer 3: settings round-trip (and run_fuzz / run_perf / ...)
./tests/web/run.sh            # Layer 4: Playwright overlay tests (?demo, headless Chromium, Node.js)
./tests/web/lint.sh           # ... + eslint over every .js in the tree (seconds, no browser)
./tests/asan/run.sh           # Layer 5: AddressSanitizer sweep (see TESTING.md)
tools/hud_window/companion_demo.sh out.png   # Layer 6: visual (instrument, not a gate)
```

Alongside the layers, the **enforced invariant checks** (fast, no Wine) fail CI
on violations - run them before pushing anything they cover:

```bash
./tests/integration/check_game_configs.sh     # GPB/KRP feature-macro syntax
./tests/integration/check_visibility_gates.sh # HUD isVisibleAnySurface() gates
./tests/integration/check_card_anchor_coverage.sh # card-box anchors are swept
./tests/integration/check_whats_new.sh          # "New" markers name this release
./tests/integration/check_api_guards.sh       # DLL-export exception barriers
./tests/integration/check_thread_safety.sh    # clang -Wthread-safety (annotated mutexes)
./tests/integration/check_mt_flags.sh         # plain bool in a thread-owning class
./tests/integration/check_move_reads.sh       # std::move(x) and a read off x in one call
./tests/integration/check_title_tier.sh       # a full HUD's caption asks for the Large tier
./tests/integration/check_hud_raw_cache.sh    # raw Unified:: members cached in a HUD
./tests/integration/check_change_consumers.sh # onDataChanged consumers state their change-gate
./tests/integration/check_test_hook_placement.sh # MXBMRP3_Test_* code only in test_hooks.cpp
./tests/integration/check_thread_join.sh      # std::thread members name their Shutdown-path join
./tests/integration/check_hud_helpers.sh      # HUD hand-rolling a BaseHud helper (12 rules)
./tests/integration/check_section_spacing.sh  # addSpacing() before a settings addSectionHeading()
./tests/integration/check_file_budgets.sh     # file-size ratchet: >1,000 lines needs a stated `// file-budget:`
./tests/integration/check_style.sh            # tabs/trailing-WS/CRLF/final newline
./tests/integration/check_pages_render.sh     # the docs through kramdown, the renderer the Pages site uses
./tests/integration/check_lazy_module_imports.sh # no import-table entry for opengl32/d3d11/dcomp (links first, so minutes not seconds)
./tests/run_cppcheck.sh                       # cppcheck static analysis (zero-finding baseline)
./tests/integration/check_session_hook.sh     # the SessionStart hook still provisions + reports
./tests/web/lint.sh                           # eslint over every .js (overlay + the web suite)
python3 tools/check_vendored_manifest.py      # vendored.json matches vendored sources
python3 tools/check_docs.py                   # doc + source-comment paths, labels, CI vs the gate list both ways
```

Separate from those, `./tests/integration/run_codeql.sh` runs **GitHub's CodeQL
security queries** over the C++ tree (CTest gate `codeql`, label `slow`, opt-in
via `./tools/install_deps.sh codeql`). It is the only gate that is *also* a CI job
elsewhere, and the only one that is opt-in; TESTING.md -> *CodeQL* has why both
are true and what a local run buys you. Budget ~10-15 min: it
rebuilds the plugin clean under the CodeQL extractor (an incremental build
compiles nothing and yields an empty, falsely-green database), then evaluates the
`cpp-code-scanning` suite (the same one the codeql-action runs). Findings
inside `mxbmrp3/vendor/**` are filtered out on both sides - we don't patch
cpp-httplib, so its alerts are permanent noise. Accepted findings live in
`tests/integration/codeql_baseline.txt`; the gate prints them as `KNOWN` and
fails only on new ones. Run it before a release, or after touching a parser, a trust boundary or a
dependency; it is deliberately not part of `-L fast`.

`check_session_hook.sh` is the odd one out: it tests a *script*, not the plugin.
`.claude/hooks/session-start.sh` is what makes an unprovisioned box
distinguishable from a healthy one - and a box missing a gate's tool produces
SKIPPED, which reads like a pass. Three consecutive fixes to that hook each
broke it a new way (silent, then session-failing, then duplicated), none of
which any other gate could see. It runs eight configurations against a stubbed
installer, sudo and toolchain, so it needs nothing installed and can never skip.

`check_style.sh` mirrors `.editorconfig`, so an editor with EditorConfig
support keeps it green for free. Code *layout* is deliberately not checked -
that script's header records why clang-format was evaluated and rejected.

Layer 1 covers pure logic compiled straight from the production headers -
formatting/color helpers, chart math, FMX scoring, the companion software
renderer, the C++/JS parity vectors (the TU census is in TESTING.md). Layer 2
cross-compiles the real plugin, drives the real PiBoSo callbacks under Wine, and
asserts the plugin's own computed state (`snapshot()`, or typed `MXBMRP3_Test_*`
hooks for internal state) - including **real-data golden masters** that replay
captured in-game callback tapes; it catches logic regressions, not just
portability breakage. Layer 3 is the specialized modalities (persistence property
test, config/callback fuzzing, CPU perf baseline). Layer 4 drives the web overlay
in a real browser. The cross-build's source divergences from the shipping MSVC
build are in [`tests/integration/README.md`](tests/integration/README.md).

### Prerequisites (Linux)

```bash
./tools/install_deps.sh              # everything
./tools/install_deps.sh --list       # what each group installs, install nothing
./tools/install_deps.sh mingw wine   # just some groups
```

`tools/install_deps.sh` is the **single source of truth** for the Linux
toolchain - the CI workflows, the SessionStart hook and this section all call it,
so there is one table to edit rather than six apt lines to keep in step. It also
applies the two fixups that are easy to miss by hand: the posix `mingw`
threading alternative (`std::thread` needs it) and the `/usr/bin/wine` launcher.
`tools/check_docs.py` fails if a CTest gate requires a binary no group provides.

By group: unit tests need `build`; the integration and specialized tests add
`mingw wine python`; the installer test adds `nsis` (plus `python`, for its
version-info assertions); the thread-safety check adds
`clang` (any recent version - it only front-end-parses, no codegen or linking);
the Layer 4 web tests and the `eslint` gate need `node` and nothing else, since
`tests/web/run.sh` fetches Playwright + Chromium on first run and `lint.sh`
shares that same npm install. `coverage`, `lint` and `analytics`
are pip-only. `codeql` is opt-in and the odd one out - no apt/pip package exists,
so the group fetches GitHub's ~1 GB CLI+query bundle into `/opt/codeql`; it is
never pulled in by a default provision.

---

## Git & versioning

- **Branches**: `claude/descriptive-name-sessionID` for AI-assisted work.
- **Version**: edit only `VER_MAJOR/MINOR/PATCH` in `mxbmrp3/resource.h`; the
  4th component is stamped at build time from the git commit count. The version
  isn't kept in git tags - but cutting a release creates a `vX.Y.Z` tag (see
  below), derived from `resource.h`. See [`CLAUDE.md`](CLAUDE.md) →
  "Version Management".

---

### Include hygiene

The transitive-include trap (a TU compiling only because some header it includes
happens to pull in what it actually uses) has a standard tool: **include-what-you-use**.

```bash
sudo apt-get install -y iwyu
include-what-you-use -std=c++17 -I mxbmrp3 -I tests/unit/shim \
    -DGAME_MXBIKES -D'__declspec(x)=' <file>.cpp
```

It is deliberately NOT wired into CI. Running it properly wants a compilation
database. CMake CAN emit one now (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` on the
cross-build tree), which removes the old objection - the Makefile could not - so
wiring IWYU into CI is a reasonable follow-up rather than the bespoke machinery
it would have been. For now: reach for it when touching includes; triage by hand.

## Releases

**Checking a branch in CI costs money in this repo.** Run the **tests** workflow
(Actions → tests → Run workflow), not **release** - the latter adds a Windows
runner billed at 2x, and its test matrix is the same one. Better still, run
`ctest --test-dir build/tests` locally: it is free, takes ~12 minutes, and the
only things it cannot cover are the CI Chromium build, the MSVC compile and the
packaging. Treat CI as confirmation, not as the iteration loop.

A release **dry run** (Create draft release OFF) skips the test matrix
deliberately - it exists to validate build + packaging, and re-running tests it
has already run is a duplicate. A REAL release always runs them and cannot be
built from a red commit.

The `release` workflow ([`.github/workflows/release.yml`](.github/workflows/release.yml))
builds all three games with MSVC on a Windows runner, **smoke-loads each built
`.dlo`** (LoadLibrary + PiBoSo export probe + GetModID check - the only place the
MSVC artifact users actually run is ever executed before shipping; the test gate
exercises the mingw build), and packages the zip + debug symbols + SBOM (generated
by [`tools/gen_sbom.py`](tools/gen_sbom.py) from
[`mxbmrp3/vendor/vendored.json`](mxbmrp3/vendor/vendored.json)), asserting the
zip's release manifest. A second job then builds the **NSIS installer on Linux**
over the MSVC payload and publishes the artifacts. It runs the **full test suite
first** (via the reusable `tests.yml`) and `build-release needs: tests`, so nothing
ships from a red commit.

The installer job is on Linux because `Target AMD64-Unicode` needs an amd64 NSIS
stub and the **official NSIS Windows distribution ships x86 stubs only** -
`nsis-3.10.zip` has thirteen, all `-x86-`. Debian/Ubuntu build the amd64 ones from
source, so `makensis` here can link what makensis on the Windows runner cannot
(it fails with `reading stub ".../lzma_solid-amd64-unicode"`, which a version
assert cannot catch because the runner genuinely has 3.10). Nothing is compiled
on Linux; the `.dlo` files are the MSVC artifacts, downloaded verbatim. It also
means the shipped installer is built by the same toolchain
`run_installer_test.sh` exercises - shipping a differently-built artifact than
the tested one is how the stub gap reached a release unnoticed.

- **Cut a release (recommended)**: Actions → **release** → **Run workflow**, tick
  **"Create draft release"**. It auto-creates the `vX.Y.Z` tag (from `resource.h`)
  at that commit and opens a **draft** GitHub Release with every asset attached
  and the passing test run linked - review, then **Publish**. It fails fast if
  that `vX.Y.Z` tag already exists (bump `resource.h` first).
- **Dry run**: same, box **unticked** - builds and uploads the artifacts (zip,
  installer, symbols, SBOM), no tag, no Release. Use it to validate.

  The button is the **only** release trigger - there is no tag-push path. The
  `vX.Y.Z` tag is created *by* the release (from `resource.h`), so it can never
  drift from the version and you never hand-create tags.
- **Locally**: [`packaging/make_release.bat`](packaging/make_release.bat) does the
  same MSVC build + packaging into `dist\` (run it from anywhere - it cd's to the
  repo root). It shares the release-notes template
  ([`packaging/release_readme.txt`](packaging/release_readme.txt)) and SBOM generator
  ([`tools/gen_sbom.py`](tools/gen_sbom.py)) with the workflow, so local and CI
  output match.

**Debug symbols** (`mxbmrp3-symbols-vX.Y.Z.zip`, `.pdb` + linker `.map` for all
three games) are archived with every release - keep them; they're what resolve a
crash dashboard's `mxbmrp3.dlo+0xNNNN` offset to a function months later.

### Publishing to the public mirror

Development happens in the private repo; users download from the public repo
(`thomas4f/mxbmrp3`), which gets **one squashed commit per release** so the
private iteration history is never exposed. The `mirror` workflow
([`.github/workflows/mirror.yml`](.github/workflows/mirror.yml)) automates that
copy - it creates a single commit whose tree is identical to private `main`,
parented on `public/main` (`git commit-tree`), and pushes it. No working-tree
copy, so it can't half-apply.

**Before you start: the one thing CI cannot tell you.** Run these locally on
`main` first - a full `ctest` is free and reproduces every Linux job, but the
CodeQL gate is opt-in and skips unless you ask for it:

```bash
ctest --test-dir build/tests                          # ~12 min, every gate but codeql
MXBMRP3_CODEQL=1 ctest --test-dir build/tests -R codeql   # ~15 min, opt-in
```

That second line is not optional diligence, it is the only pre-release scan that
exists: [`codeql.yml`](.github/workflows/codeql.yml) is gated to the PUBLIC
mirror, and the mirror only receives code at step 4 below - so without the local
gate, the first CodeQL scan of a change happens *at* its release. That is exactly
how v1.28.0's three alerts arrived. `ctest` alone cannot substitute: `codeql`
skips silently in it, and a SKIP is not a pass.

The end-to-end release flow (all buttons, no local git):

1. Bump `VER_*` in `mxbmrp3/resource.h`, move [`CHANGELOG.md`](CHANGELOG.md)'s
   `## [Unreleased]` entries under the new version heading (with the date and a
   compare link), and merge everything into private `main`. The changelog is the
   source for the Release body - write it for players, not for reviewers.
2. **Private** → Actions → **tests** (Run workflow), then **release**, box
   **unticked** (dry run). Two separate buttons: a dry run SKIPS the test matrix,
   so `tests` is what tells you the commit is green and `release` only tells you
   it builds. Inspect the built zip/installer artifacts.
3. **Private** → Actions → **mirror**, box **unticked** - preview
   `git diff --stat public/main..HEAD` (the "verify before push" step).
4. **Private** → Actions → **mirror**, box **ticked** - push the squashed tree
   to `public/main`, and tag private `vX.Y.Z` at the commit that was mirrored.
5. **Public** → Actions → **release**, box **ticked** - build/test/tag/draft the
   Release *where users download*. Building on the public tree means the
   FILEVERSION 4th component (`git rev-list --count HEAD`) matches a rebuild from
   public source. Review, then **Publish**.

**Both repos carry the `vX.Y.Z` tag, and neither is hand-made.** In public it is
created by `release` at step 5, because a GitHub Release must be tag-backed. In
private it is created by `mirror` at step 4, at the commit whose tree was
published - so "which commit shipped as 1.29.0" is answerable on this side too,
instead of needing the trees matched by hand. The private tag is annotated with
the public commit it produced, and the two point at identical trees by
construction.

Re-mirroring the same version is normal (mirror, spot something, fix, mirror
again), so **the private tag moves while the version is unreleased**. Once the
public repo has that tag the version has shipped and the private tag stays put:
a later mirror still publishes, it just doesn't drag the tag off what users got.
Bump `resource.h` for the next one - the same rule `release` enforces, one step
earlier.

The `mirror` step is **source-only** - it does not run the test suite, so it can
push `main`'s tree to public before any CI on that exact commit finishes. The
release gate lives downstream in step 5 (`release` → `build-release needs: tests`),
so a *binary* never ships red. Step 2 is therefore two runs, not one: the dry-run
**release** proves `main` BUILDS and packages, and it deliberately skips the
matrix, so only the **tests** run beside it proves `main` is GREEN. Both before
you mirror, so the public source and the shipped binary stay in lockstep - the
mirrored tree is otherwise the one thing here nothing has checked. `mirror`
also refuses to run from any ref other than the default branch.

**One-time setup**: the public repo needs the `APTABASE_KEY` / `GOATCOUNTER_TOKEN`
Actions Secrets (a Release build hard-errors without them); the private repo needs
a `PUBLIC_REPO_TOKEN` secret - a fine-grained PAT with `contents: write` on
`thomas4f/mxbmrp3` - for the mirror push. `packaging/make_release.bat` stays as the
offline/local build path; it shares the template + SBOM generator with the
workflow, so its output matches CI.
