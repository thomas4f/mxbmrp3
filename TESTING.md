# Testing MXBMRP3

The shipping plugin is a Windows-only MSVC DLL, but its **logic** is portable and
tested on Linux with no game and no Windows. Everything here runs in CI
(`.github/workflows/tests.yml` - on demand via **Run workflow**, as the release
workflow's gate, and automatically on pull requests in the free public mirror; see
the workflow header for why there is no push trigger) and locally with a C++17
compiler + (for the integration layer) mingw-w64 and Wine.

There are six layers, fastest first. Reach for the cheapest one that can
exercise your change (the table in `CLAUDE.md` → *Testing Discipline* maps a
change to its layer). Layers 1–5 are asserted and CI-gated; Layer 6 (visual) is
an instrument you point at a change, not a gate.

**To run everything at once, use CTest** - every gate below plus the invariant
lints is registered in `CMakeLists.txt` (which builds nothing; it exists only to
register tests):

```bash
cmake -S . -B build/tests                             # once
ctest --test-dir build/tests --output-on-failure      # everything
ctest --test-dir build/tests -L fast                  # no mingw/wine needed
ctest --test-dir build/tests -j 4 -R settings         # parallel, by name
```

A gate whose toolchain is absent exits 3 and CTest reports it **SKIPPED** rather
than failed - `SKIP_RETURN_CODE`, CTest's own convention - so the suite is useful
on a box with nothing but `g++`. Prefer this over hand-chaining the per-layer
scripts: the chain is long enough to exceed an automated runner's command
timeout, which is how a full verification ends up stranded half-done.

**A SKIP is not a pass, and it is not a result.** It means that check did not
run, so whatever it guards is unverified. On a dev box or in an agent session,
the expected response is to INSTALL the tool and get a real answer, not to
report the suite green with skips in it:

```bash
./tools/install_deps.sh --list        # groups -> what each provides
./tools/install_deps.sh cppcheck      # then the group you need
```

The skip semantics exist so a contributor with only `g++` can still run the fast
layer - not as a way to opt out of a gate that is inconvenient to provision. The
SessionStart hook (`.claude/hooks/session-start.sh`) installs every group up
front and prints a `MISSING tools:` line naming anything it could not, so an
absent tool is visible once at startup instead of only as a SKIPPED line
scrolling past mid-run.

| Layer | Framework | Needs | Runtime | Runner |
|---|---|---|---|---|
| **Unit** - pure header logic | doctest | just `g++` (+ CMake) | ~1s run (~20s cold compile) | `ctest -R '^unit'` |
| **Integration** - real plugin, driven headless | doctest + Wine | mingw-w64, wine64 | ~2 min warm (ccache); ~5-8 min cold (full cross-build + one Wine binary per `tests/*.cpp`) | `tests/integration/run_tests.sh` |
| **Specialized** - persistence / fuzz / perf / installer | bespoke | mingw-w64, wine64, python3 | ~1–3min | `tests/integration/run_*.sh` |
| **Web overlay** - rendered DOM in a real browser | Playwright | Node.js | ~40s | `tests/web/run.sh` (see the browser caveat below); `tests/web/lint.sh` for the eslint gate |
| **Memory safety** - ASan/UBSan over the portable memory surface | doctest + a targeted harness | g++/clang, libasan | ~seconds | `ctest -R unit-asan` + `tests/asan/run.sh` |

Two gates sit outside the layers because they cover a *tool*, not the plugin.
Both ran in CI for months without being gates - so `ctest` was green while CI ran
more than it, and that is how 19 compiler warnings per build sat unread in a CI
log. `check_docs.py` now checks that direction too, so a CI step that isn't a
gate fails the docs check:

| Gate | What it covers |
|---|---|
| `fontgen` | `tools/fontgen/test.sh` - regenerates `RobotoMono-Regular.fnt` from the source `.ttf` and asserts it is structurally identical to the shipped font (cell height, per-glyph advances, atlas dims, inflate round-trip, mip-safe glyph gaps) |
| `outlines2ttf` | `tools/fontgen/test_outlines2ttf.sh` - the outline-sheet importer (`outlines2ttf.py`: artwork `.ai`/`.pdf`/`.svg` -> `.ttf`, then fontgen -> `.fnt`). |
| `themeslice` | `tools/themeslice/themeslice.py --selftest` - the theme slicer's **round trip**: cut a synthetic asymmetric master into 27 slices and require each to equal its region of the source (symmetric input cannot detect a transposed cell, which is the failure that ships two corners wrong). |
| `analytics-selftest` | `tools/analytics_report.py --selftest`. Needs pandas + markdown-it-py (`./tools/install_deps.sh analytics`), else SKIPs |
| `icon-repro` | `tests/integration/check_icon_reproducibility.sh` - regenerates all 208 shipped `mxbmrp3_data/icons/*.tga` from `assets/icons/*.svg` under the **pinned** cairosvg and requires a byte match, both set directions included. |
| `analytics-fetch-selftest` | `tools/analytics_fetch.py --selftest` - the Aptabase fetcher's rejections. |

Alongside the test layers, CI also runs **cppcheck** static analysis
(`.github/workflows/tests.yml`, over `mxbmrp3/` with vendored code excluded). It is
**blocking**: the committed baseline is at zero findings, so any new one FAILS the
build. It was report-only until that baseline was actually driven to zero -
findings nobody has to act on are findings nobody reads, and two error-severity
ones had accumulated unnoticed in job summaries (a `danglingLifetime` in
`PluginThread::flush()` that turned out to mark a real hang, and an out-of-bounds
copy count in `RecordsHud`). The accepted cost is that cppcheck versions drift
between runner images, so a toolchain bump can surface a finding unrelated to
your diff. To land a legitimate one: fix it, add an inline
`// cppcheck-suppress <id>` with a reason, or - last resort - a documented entry
in `.cppcheck-suppressions` (which holds only project-wide intentional patterns).
To run it locally: **`./tests/run_cppcheck.sh`** - the same script CI invokes, so
the flags can't drift from what you reproduce (`--report` prints findings without
failing).

CI also runs the **enforced invariant checks**, which likewise FAIL the build.
Each script's header documents the invariant, why it is a lint, and its
escape-hatch annotation; CLAUDE.md → *Maintenance Invariants* maps rule →
enforcement. A `--self-test` flag, where a script has one, is its own gate.

- `tests/integration/check_game_configs.sh` - GPB/KRP syntax
- `check_visibility_gates.sh` - HUD `isVisibleAnySurface()` gates
- `check_card_anchor_coverage.sh` - every user of the card-box accessors is swept by `card_anchor_sweep_test`
- `check_title_tier.sh` - a full HUD's caption asks for, and is measured at, `TitleTier::Large`
- `check_lazy_module_imports.sh` - the cross-built DLL imports none of `opengl32`/`d3d11`/`d3dcompiler`/`dcomp` (all resolved at runtime, so the GPU -> software -> engine fallback has nothing for the loader to fail on)
- `check_api_guards.sh` - DLL-export exception barriers
- `check_thread_safety.sh` - no raw `std::mutex`/`lock_guard`/`unique_lock` outside `core/thread_safety.h` (the analysis cannot see them)
- `check_mt_flags.sh` - a plain `bool` in a class owning a `std::thread` is `std::atomic`, `MXB_GUARDED_BY`, or carries `mt-plain:`
- `check_clang_tidy.sh` - clang-tidy with only the checks in `mxbmrp3/.clang-tidy`, today `bugprone-use-after-move`, plus clang `-Wthread-safety` over annotated mutexes (see `core/thread_safety.h`) on the same parse; slow, minutes
- `check_change_consumers.sh` - every `X::onDataChanged` carries a `// change-gate:` annotation
- `check_test_hook_placement.sh` - `MXBMRP3_Test_*` lives only in `core/test_hooks*.cpp`, the files excluded from every shipping target
- `check_thread_join.sh` - every first-party `std::thread` member names its joiner on the `Shutdown()` chain (`// joined-by:`)
- `check_style.sh` - file hygiene (tabs, trailing whitespace, CRLF, final newline), mirroring `.editorconfig`
- `check_pages_render.sh` - every tracked `.md` through **kramdown**, the GitHub PAGES renderer, failing on output that silently degrades there (SKIPs without the gem)
- `check_session_hook.sh` - the SessionStart hook's own behaviour across eight configurations
- `tools/check_vendored_manifest.py` - vendored.json vs the vendored sources
- `tools/check_docs.py` - path references in the .md files **and** in first-party source comments, plus the rest of its docstring's list

Two more gates round out the analysis side: **`python-lint`** (`ruff check .`
over the repo's Python - the dev tools and doc checkers) and
**`shim-constants`** (`tests/unit/shim/regen_constants.sh --check`, which
re-derives the shim's copied API constants and fails if they drifted from the
vendored headers, so a hand-edited constant can't silently disagree with the
game's).

### CodeQL (deep static analysis, opt-in)

**`./tests/integration/run_codeql.sh`** runs GitHub's own CodeQL security
queries over the C++ tree - the `cpp-code-scanning` suite, the same one the
codeql-action evaluates. It is analysis, not a test layer: like cppcheck it reads
the code rather than running it, which is why it sits here and not as a seventh
layer above.

It exists because **`codeql.yml` can only run on the public mirror** (code
scanning needs Advanced Security, which the private repo doesn't have) and the
mirror only receives code at release time - so without a local run, the first
CodeQL scan of any change is its release. That is literally how v1.28.0 shipped
and then collected three alerts.

It is the **only opt-in gate**: a bare `ctest` skips it, even where the CodeQL
bundle is installed. Labels select rather than exclude, so `slow` alone wouldn't
have kept a 15-minute scan out of the default run once anyone had installed the
tool once - and a suite that costs a quarter of an hour after a one-line edit is
a suite people stop running.

```bash
./tools/install_deps.sh codeql                                  # ~1 GB bundle, once
MXBMRP3_CODEQL=1 ctest --test-dir build/tests -R codeql          # via CTest
./tests/integration/run_codeql.sh                                # or directly
./tests/integration/run_codeql.sh --keep-db                      # reuse the database
```

Budget ~10–15 min: it rebuilds the plugin clean under the CodeQL extractor, then
evaluates the queries. Reach for it **before a release**, or after touching a
parser, a trust boundary, or a dependency - not in an edit-compile-test loop.

Two guards protect the result from being falsely green, and both exist because
the failure happened:

- **Empty database.** An incremental build compiles nothing the extractor can
  observe, producing a database that analyzes to zero findings. `CCACHE_DISABLE=1`
  plus `-B` prevents it; a missing `db-cpp/` fails loudly.
- **Partial database.** The subtler one: a run that extracted **42 of 444 files**
  reported `no findings` and exited 0, indistinguishable from a real pass - the
  code the finding lived in simply wasn't in the database. The script now reads
  CodeQL's own scanned-file count and fails below a floor (a healthy run scans
  ~307). Raise the floor as the tree grows; never lower it to make a run pass.

Accepted findings live in `tests/integration/codeql_baseline.txt` - printed as
`KNOWN` and excused, everything else fails. Entries take a source-path substring
as well as a sink, because results are keyed on the *sink*, and a sink like
`logger.cpp` is shared by every log line in the plugin. The file is currently
empty by design: prefer deleting the flow to accepting it.

The script pins **`--enable=warning`** for a reason. The `.cppcheck-suppressions`
baseline is curated for that severity only - broadening to
`--enable=warning,performance,portability` surfaces extra classes CI doesn't gate
(e.g. `memsetClassFloat` on the POD `Unified::` structs, `uselessCallsSubstr`), which
look like "new findings you have to filter" but are just the wider net, not a hole in
the suppressions.

## Principles (read this before adding a test)

A handful of ideas shape the whole suite. None of them are local inventions -
each is a named, established practice, noted below so this reads as *convention
applied here*, not house style. They're worth internalising once; the per-layer
sections are just these principles applied. The tests themselves follow
**Arrange–Act–Assert**: set up the scenario, drive one callback, snapshot and
assert (see any `*_test.cpp`).

1. **Test behaviour through the real seams, not the implementation**
   (*test-through-the-public-API*; "behaviour over implementation"). The
   integration layer drives the *actual* PiBoSo callbacks into the *actual*
   compiled plugin and reads the plugin's *actual* output. It doesn't reach inside
   to poke private members or re-implement the math. This is
   **characterization / golden-master testing**: pin what the plugin *does* end to
   end, so a refactor that preserves behaviour stays green and one that breaks it
   goes red - regardless of how the internals move. A test that knows too much
   about the internals breaks on every refactor and stops being trusted.

2. **Prefer the black box; reach for the white box only when the value never
   surfaces.** Default to asserting the plugin's stable public output - the
   `/api/state` JSON snapshot (via `host.snapshot()`) - because that's a contract
   real consumers depend on, so a test against it is a test of something that
   matters. Only when a computation genuinely never reaches that output (the
   in-game-only real-time gap is the canonical case) do you open a typed
   **white-box hook** (`MXBMRP3_Test_*`) - a **seam** (Feathers), a test-only
   access point compiled out of the shipping DLL - and assert the internal value
   directly. Don't distort the product - don't add a field to the data contract
   just to make it testable - and don't leave the logic untested; add a hook. Keep
   hooks scarce: each one is a coupling to internals, so the bar is "the value
   genuinely never surfaces," not "it's easier." (See *Test-only hooks*.)

3. **Test the logic in isolation from the plumbing** (*hermetic tests*). A
   plugin-logic test must
   depend only on the plugin's *computation*, never on the HTTP server, sockets, or
   the snapshot-rebuild gating that sits in front of it in production.
   `host.snapshot()` calls `buildJsonSnapshot()` directly for exactly this reason.
   Only the http tests (`http_test.cpp`, `http_robust_test.cpp`,
   `http_sse_test.cpp`) exercise the serving path itself. When a
   test needs a workaround to satisfy machinery it isn't testing (an earlier
   version had to fire a dummy update just to defeat the rebuild gate), that's the
   signal a layer is coupled that shouldn't be - fix the seam, don't paper over it.

4. **Synthetic tests for precision, real-data golden masters for fidelity.**
   Hand-authored callback streams are deterministic and let you construct the exact
   edge case (a reused race number, a spurious lead, a DSQ) - but they're only as
   correct as *our reading* of the API. A **real captured tape** replayed
   headlessly (`replayTape()`) is the fidelity anchor: it proves the synthetic
   inputs match what the game actually sends. Keep both - they catch different
   failures. A note on golden masters, which have a deserved reputation for being
   brittle and opaque: ours assert **specific, meaning-bearing values
   cross-checked against the session log** (this rider won, this gap, this
   penalty), never a blind blob/byte diff - a *semantic* golden master, so a
   failure names what broke instead of "output changed." (See *Real-data replay*.)

5. **Keep the whole master, commit a slim fixture.** Slimming is one-way, so a
   git-ignored master is archived whole and a small per-test fixture is committed
   from it. `tests/integration/tapes/README.md` has the rule, `slim_tape.py`'s
   profiles, and what is still worth capturing.

6. **Push each test to the cheapest layer that can still exercise it** (the
   **test pyramid**: many fast unit tests, fewer integration, fewest browser
   e2e). A pure formatting helper is a ~1s unit test, not a 30s Wine round-trip.
   The mapping from "what you changed" to "which layer" lives in `CLAUDE.md` →
   *Testing Discipline*; the fast layers exist so there's no excuse to skip a test
   because "the real one is slow."

7. **A gap you can see is a managed risk; a gap you can't is a latent bug.**
   `API_COVERAGE.md` is a behavioral **coverage manifest** of every callback and
   its status. It's deliberately a manifest, not a line-coverage percentage: the
   cross-build is a *different* configuration from the shipping MSVC DLL, so a
   coverage number would measure the test build, not the product - and the goal is
   that untested *surface* is visible, not that every line is hit. When you find a
   gap you can't close now, write it down (there and/or as a `Known gap` note)
   rather than leaving it silent.

8. **A test explains itself in its own header; the catalogue below only names
   it.** The per-layer lists are a *census* - enough to pick which file to open,
   and enough that a new test cannot land undocumented. What the test pins, the
   bug it would have caught, the trap the obvious implementation falls into: all
   of that goes in the file, where it is re-read exactly when someone opens it.
   This is the same rule CLAUDE.md applies to mechanism detail, for the same
   reason - the catalogue entries here used to carry the explanation *too*, and
   the duplicate was the half that rotted (it named `kShippedPacks` long after
   the code renamed it `kPublishedPacks`, while the header stayed right).
   **Enforced** by `check_test_headers.sh` (the `test-headers` gate): a test file
   whose header is under 400 bytes fails, because with the catalogue no longer
   explaining, a thin header leaves the "why" nowhere at all.

## Layer 1 - Unit tests (`tests/unit/`)

Pure, platform-independent functions (color math, time/score formatting, hex
parsing) compiled straight from the production header and checked with
[doctest](https://github.com/doctest/doctest). No game, no singletons, no
Windows.

```bash
ctest --test-dir build/tests -R '^unit'                  # build + run all three flavours
cmake --build build/tests --target unit_tests \
  && ./build/tests/tests/unit/unit_tests -tc='*hex*'     # doctest filter
```

Add a case to `tests/unit/test_plugin_utils.cpp` (or a new `tests/unit/test_*.cpp`, then
list it in `tests/unit/CMakeLists.txt`). A function belongs here iff it depends on
nothing but the C++ standard library - anything reaching into `PluginData` or the
game API is an integration test instead. The authoritative TU list is
`MXB_UNIT_SOURCES` in `tests/unit/CMakeLists.txt` (census:
`ls tests/unit/test_*.cpp`); that list also compiles the production
`mxbmrp3/core/ui_config.cpp` under test. What each pins:

- `test_plugin_utils.cpp` - color/time/hex helpers in `core/plugin_utils.h` (also owns the doctest impl + `main`)
- `test_notice_priority.cpp` - `hud/notice_priority.h`, the masked-notice display-timer decision
- `test_twitch_irc.cpp` - `core/twitch_irc.h`: IRC parsing, moderation, CP1252, emotes, wrap
- `test_youtube_chat.cpp` - `core/youtube_chat.h` against `tests/fixtures/youtube/`
- `test_hold_repeat.cpp` - `core/hold_repeat.h`
- `test_text_edit.cpp` - `core/text_edit.h`
- `test_hotkey_names.cpp` / `test_slider_control.cpp` - hotkey names fit; slider mapping
- `test_system_messages.cpp` - `core/system_messages.h`: the toast queue and which startup popup is owed
- `test_achievements.cpp` - `core/achievements.h` + `core/exploration_signals.h`: the catalogue's own invariants (unique ids, ascending thresholds within a row's tier count, a `%s` in every tiered template, a sentence for every one-shot, hidden rows on the Hidden page), the removal rule (every metric and every signal feeds exactly one row, every disabled id is a real row), tier stepping, one-shot tags, the step-relative progress bar, the km/h/s/count formatting, and `docs/achievements.md`, which it GENERATES from the table and diffs against the committed copy
- `test_analytics_remote_config.cpp` - the remote sampling cost lever (`parseFullSample`/`shouldSendFull`): fails open to full, deterministic 0.0/1.0 endpoints
- `test_analytics_endpoint.cpp` - App-Key → Aptabase ingest-region routing (unknown/self-hosted → "" = no send)
- `test_analytics_redact.cpp` - the install id is stripped from a logged request body.
- `test_log_scrub.cpp` - no log line names the user folder.
- `test_analytics_identity.cpp` - recovering the install id and counters from a damaged analytics file (`core/analytics_identity.h`): whole values only, garbage refused, a whole file reads as through the parser.
- `test_analytics_theme.cpp` - the `panel_theme` label (none/shipped/custom/missing, so a user's own theme name never ships) **and** the drift guard: it walks `mxbmrp3_data/themes/` both ways, so adding a shipped theme without listing it in `AnalyticsTheme::kShippedThemes` fails here instead of silently filing its users under "custom"
- `test_director_airtime.cpp` - the director's two airtime helpers: the lull round-robin (`pickNextAirtimeNum`), whose cursor keys on race number rather than grid position, and the dead-air floor (`pickBaselineSubject`), which hands the camera to the broadcaster's own rider once forced rotation is off - and degrades to the leader when that rider is gone, so "Max shot = Off" can never mean dead air
- `test_session_charts_math.cpp` - race-progression chart maths in `hud/session_charts_math.h`
- `test_tooltip_length.cpp` - every settings tooltip fits the 2-line/~120-char render limit (compiles the real tooltip table)
- `test_update_asset_select.cpp` - the updater's release-asset picker (the symbols-zip-matched-first regression)
- `test_update_version_match.cpp` - `UpdateChecker::isSameRelease`, the "is the running DLL the update we just installed?" test.
- `test_ui_config.cpp` - INI-only grid-overlay defaults + the `majorEvery` clamp
- `test_render_frame_buffer.cpp` - the plugin-worker-thread triple buffer: the producer never writes the displayed slot; `acquire()` returns the latest published frame
- `test_crash_stack_format.cpp` - the crash handler's backtrace string formatting + the whole-frame `MAX_STACK_CHARS` budget
- `pixel_text_test.cpp` - `core/pixel_text.h`, the missing-assets warning's quad alphabet.
- `test_render_batch.cpp` - the shared batcher (`core/render_batch.h`), extracted from the D3D11 backend so the GL backend consumes the same batches.
- `test_gl_state_fingerprint.cpp` - the comparison half of the GL state-leak check (`core/gl_state_fingerprint.h`).
- `test_hud_sw_renderer.cpp` - golden-frame sampling of the companion window's software renderer (`core/hud_sw_renderer.cpp` compiled natively): quad fill, per-quad alpha, the texel×color modulate (white-icon tinting), `.fnt` text against a real shipped font, and the scale-viewport mapping.
- `test_render_asset_decode_mips.cpp` - the glyph-atlas mip chain (`hudassets::buildFntMips`), which all three renderers sample text from, so a fault here is a fault in every backend at once.
- `test_hud_sw_assets.cpp` - malformed `.fnt`/`.tga` input to the same renderer's two binary parsers.
- `test_fmx_scoring.cpp` - FMX trick scoring (`core/fmx_manager` math): rotation scale floors at 1×, air/ground tricks scale with duration (floored) and distance, and `docs/tricks.md`, which it GENERATES from `core/fmx_types.h` (every trick, its INI key, base score, axis, progress and recognition gates) and the scoring defaults, then diffs against the committed copy
- `test_segment_cumulative.cpp` - cumulative custom-segment timing: a contiguous run aggregates like the official splits; on-sector identity; isolated-arc fallback
- `test_blue_flag_detect.cpp` - the blue-flag/lapping proximity core (`core/blue_flag_detect.h`): start/finish wraparound, directionality, the deliberately asymmetric backmarker-vs-lapper eligibility, stale-sample rejection, the first-lapper-wins ordering the director depends on, and output clearing (the containers are reused every rebuild).
- `test_roost_detect.cpp` - `core/roost_detect.h`, the pairwise proximity core behind Roost (Side by Side no longer has a row, but is still classified - it is how the detector says "alongside, not behind").
- `test_battle_groups.cpp` - the battle-group partitioning core (`core/battle_groups.h`) behind the director's battle scoring and the overlay battle panel: adjacency chaining (each consecutive delta within threshold, regardless of total spread), the strictly-positive-delta rule that keeps the pre-first-split field (everyone at gap 0) from fusing into one giant group, lap boundaries never chaining, the maxLeaderPos group filter, and position-sort independence from input order.
- `test_lap_timer.cpp` - the display rider's live lap-timer state machine (`core/lap_timer.h`): S/F detection, the grid-start grace and its exits, split resync, invalidation.
- `test_delta_color.cpp` - the one colour rule for a time delta (`deltaColorSlot`): ahead, behind, exactly even.
- `test_split_crossing.cpp` - `timeToCrossing` (GP Bikes' third split included) and `HoldTimer`.
- `test_pb_gap_tracker.cpp` - the live gap-to-PB engine (`core/pb_gap_tracker.h`): commit gated on an observed lap start, the lap fence in both orders, sign and interpolation, the sample fallbacks, the ghost position, the three references, the last lap's gaps, and what forget/clear/reset each drop.
- `test_lap_delta_rate.cpp` - the Map's lap delta colouring (`LapDeltaRate` in `core/lap_delta_profile.h`): gain reads negative and loss positive, the lap's largest slope scales to 1, the rate moves smoothly between profile points, nothing is read past the last sampled point, and the previous lap shows past the overwrite gap; plus `PluginUtils::mixColor`.
- `test_pb_trace_store.cpp` - the persisted PB tables (`core/pb_trace_store.h`): round-trip with holes, malformed input refused alone, fastest lap per key, clear.
- `test_live_gap_engine.cpp` - the live leader-relative gap core (`core/live_gap_engine.h`): gap direction per session format (countdown vs count-up clocks - reversed subtraction shows every gap frozen), the freeze-vs-set semantics (stale/finished/unstamped/non-positive all keep the last shown value; leader and lapped riders are explicitly SET), the sub-threshold return that keeps full grids from rebuilding every HUD per 30Hz batch, trackPos quantization incl. the 1.0 clamp, and lap pruning.
- `test_marker_label.cpp` - the shared rider-marker label core (`hud/marker_label.h`) that Map/Radar/GapBar all render through: the exact `P%d [#%d]` text per mode, the no-position fallbacks (POSITION renders nothing, BOTH drops to "#%d"), podium gold/silver/bronze applied only in position-showing modes, and the enum's numeric values, which are GapBar's on-disk INI representation.
- `test_settings_serde.cpp` - the enum<->string converter pairs in `core/settings_serde.h` + `core/settings_serde_hud.h`, the entire on-disk representation of every enum setting.
- `test_director_scoring.cpp` - the auto-director's story-score formulas (`core/director_scoring.h`).
- `test_director_detect.cpp` - the overtake/drop edge detectors (`core/director_detect.h`).
- `test_camera_resolve.cpp` - spectate camera-name matching and role->index resolution (`handlers/camera_resolve.h`).
- `test_standings_context_window.cpp` - the standings pagination (`hud/standings_context_window.h`): which slice(s) of the classification the table draws.
- `test_records_window.cpp` - the records table's context window (`hud/records_window.h`): which slice of the fetched records surrounds the player's own PB.
- `test_font_metrics.cpp` - the two `LayoutMetrics` numbers that are MEASUREMENTS of the shipped `.fnt` rather than style choices, checked against the atlases themselves.
- `test_gamepad_geometry.cpp` - the gamepad widget's unit system (`hud/gamepad_geometry.h`).
- `test_asset_packs.cpp` - the SHIPPED asset packs (gamepad pads and pit boards)' inis still describe the pads they replaced.
- `test_plate_geometry.cpp` - the standings race-number plate box (`hud/plate_geometry.h`). One property, and it is what everything about the number's placement rests on: the plate is **centred in its row**.
- `test_lap_log_plan.cpp` - the Lap Log's row planning (`hud/lap_log_plan.h`): which rows are drawn and in what order.
- `test_icon_resolve.cpp` - icon sprite resolution both ways (`core/icon_resolve.h`): a name or a 1-based shape index to the sprite that gets drawn, and a sprite back to the shape index it stands for.
- `test_severity_ramp.cpp` - the shared gauge colour ramp (`hud/severity_ramp.h`): a reading as a fraction of full scale to a colour on the palette's POSITIVE → NEUTRAL → NEGATIVE ramp, used by the G-force ring and the Lean widget's arc and steer bar.
- `test_radar_fade.cpp` - the radar's auto-hide fade (`hud/radar_fade.h`): how visible the radar is given who is nearby.
- `test_digit_roll.cpp` - Motion's digit roll (`hud/digit_roll.h`).
- `test_motion.cpp` - Motion's timing (`core/motion.h`): fades start settled, turn round mid-way, and step at most 50 ms a frame.
- `test_peak_marker.cpp` - the shared "max marker" state machine (`hud/peak_marker.h`), which replaced six character-for-character copies across LeanWidget (lean and steer, both sides), BarsWidget and RumbleHud.
- `test_standings_gap_plan.cpp` - the standings gap-column decision (`hud/standings_gap_plan.h`): which value each cell shows, in which style, with which tint.
- `test_thread_detach_grace.cpp` - the spin-then-detach teardown POLICY (`core/thread_detach_grace.h`), shared by the three singletons that own a worker thread.
- `test_text_wrap.cpp` - the settings tooltip box's greedy word wrap (`hud/settings/text_wrap.h`), extracted from a lambda in `rebuildRenderData()` that wrapped and emitted in one loop.
- `test_spotter_phrase.cpp` - the spotter's phrase composition (`core/spotter_phrase.h`): the racing-style number words ("four seventy six", "two oh six") asserted verbatim against `tests/fixtures/spotter_number_words.txt` - the SAME fixture the pack generator's `spottergen-selftest` gate asserts, so C++ wording and every baked `num_*.wav` can only pass together - lap times speaking tenths only (".007" is zero tenths), "you" vs "rider N" phrasing incl. the raceNum -1 guard that keeps session events from matching a -1 focus, and the empty-string "never spoken" contract (Director cues, your own retirement)
- `spotter_test.cpp` *(integration)* - the cue pipeline through the real callbacks: race events -> `addEventLogEntry` tap -> enable/category gates -> composed text in the cue log (what the subtitle widget shows; audio is not asserted - a Wine prefix has no SAPI voice and the worker degrades by design).
- `test_spotter_hazard.cpp` - the spotter's proximity/hazard cue state machine (`core/spotter_hazard.h`): edges and restraint over detection that lives in PluginData.
- `test_spotter_milestones.cpp` - the spotter's session-progress milestones (`core/spotter_milestones.h`): "ten minutes to go" / "five minutes left" / "halfway there" as crossing-edged, once-per-session calls.
- `test_analytics_spotter.cpp` - the spotter analytics label (`core/analytics_spotter.h`), the panel-theme classifier applied to the spotter: ONE property where off is a value (`"none"`), so adoption is `spotter != "none"` rather than a separate flag.
- `test_spotter_tts_voice.cpp` - the in-game TTS voice picker's pure half (`core/spotter_tts_voice.h`).
- `test_spotter_pace.cpp` - the spotter's pace-report tracker (`core/spotter_pace.h`): gap to the rider ahead/behind at timing points with a gaining/losing trend.
- `test_spotter_mix.cpp` - the spotter's wav chunk mixer (`core/spotter_mix.h`). parseWav is a trust boundary (chunk wavs arrive in SHARED packs): the malformed cases - lying chunk sizes, truncation, stereo/float/8-bit formats, data-before-fmt - must reject whole, and the `unit-asan` flavor is what gives the bounds checks teeth.
- `test_install_prefs.cpp` - the installer's analytics opt-out marker (`core/install_prefs.h`), which is how Setup's Privacy-page choice reaches a plugin whose settings file does not exist yet.
- `test_pack_ini_path.cpp` - the pack-ini resolution rule (`core/pack_ini_path.h`) with the filesystem replaced by a set of paths.
- `test_pack_types.cpp` - censuses `AssetManager::PACK_TYPES` (the one table both user-asset copies walk: the startup sync and the RELOAD_CONFIG re-copy) against the shipped `mxbmrp3_data/`.
- `test_spotter_pack_census.cpp` - walks the SHIPPED pack (`mxbmrp3_data/spotters/default/spotter.ini`) against the two published namespaces, in both directions.
- `test_completion_floor.cpp` - `core/completion_floor.h`, the Sweep rows' per-metal percentages, which replaced a single lowest-tier FLOOR that sat at zero until the last row moved.
- `test_finish_margin.cpp` - `core/finish_margin.h`, the winner-to-runner-up margin behind Photo Finish.
- `test_fuel_estimate.cpp` - the fuel arithmetic (`core/fuel_estimate.h`), shared by the Fuel widget's readout and the spotter's warning.
- `test_spotter_vars.cpp` - the `{variable}` namespace (`core/spotter_vars.h`), which is FROZEN once packs are written against it.
- `test_spotter_stretch.cpp` - the pitch-preserving time stretch (`core/spotter_stretch.h`) behind the speed setting on the wav paths.
- `test_spotter_cue_pack.cpp` - the cue-pack format (`core/spotter_cue_pack.h`), the contract every shared pack is written against: `cueKeyFor`'s stable key names (a rename orphans every pack's override of that cue), the tolerant parse with empty-value-as-mute distinct from absent-as-fallback, the `_wav` path-escape rejection (a shared pack naming `..\..\x.wav` must never reach PlaySound), and `expand`'s punctuation tidy-up that lets one template serve events with and without a lap time
- `test_spotter_queue.cpp` - the spotter's pending-cue queue (`core/spotter_queue.h`): FIFO order across mixed cue kinds, the drop-OLDEST overflow rule, and the expiry of `perishable` cues at pop.
- `test_cpp_js_parity.cpp` - the C++ side of the cross-renderer mirror vectors (`tests/fixtures/cpp_js_parity.json`): `PluginUtils::isColorDark` and `session_charts_math.h` `formatSecs` against the SAME golden file `tests/web/tests/parity.spec.js` asserts on the overlay JS - a one-sided edit fails one of the two suites
- `asset_path_test.cpp` - `AssetPath::renderName` (`core/asset_path.h`): the display name a discovered asset gets.
- `history_ring_test.cpp` - `HistoryRing<T, N>` (`core/history_ring.h`): a fixed-capacity rolling history, including the wrap.
- `small_vec_test.cpp` - `SmallVec<T, N>` (`core/small_vec.h`): inline storage up to N, heap spill beyond, and the move paths.
- `viewport_test.cpp` - `UiViewport::compute` (`core/ui_viewport.h`): the ONE centered-16:9 UI-rect computation the companion paint loop and InputManager's cursor / window-bounds maps all share - previously three inline copies (integer vs float truncation) that could disagree by a pixel at odd client sizes, so a click landed beside the thing it clicked.
- `nine_slice_test.cpp` - the pure geometry of `hud/nine_slice.h`: how a slice grid divides a rect.
- `panel_box_test.cpp` - `core/panel_box.h` against the box model's golden vectors.
- `layout_ini_test.cpp` - the layout/theme ini FORMAT, one line at a time.
- `layout_metrics_test.cpp` - the layout vocabulary as data: the derived values still agree with the terms they come from.
- `grid_snap_test.cpp` - panel ORIGINS land on the snap lattice, not just the rows inside them.
- `gauge_square_test.cpp` - a gauge's dial stays circular while its box lands on the cell lattice.
- `gear_geometry_test.cpp` - the gear digit is sized from the FONT and capped by its box, so a raised `uiLineHeight` gives it air instead of a bigger glyph that runs off the panel.
- `center_stack_test.cpp` - the three centred top panels do not overlap each other or the screen edge.
- `corner_button_test.cpp` - the settings gear and director camera do not overlap and stay on screen.

Exactly one TU defines the doctest impl + `main`
(`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`); every other TU just `#include "doctest.h"`
with no config macro, or the impl is defined twice and the link fails.
The `unit_tests_asan` target rebuilds the same suite under AddressSanitizer +
UBSan - see *Layer 5 - Memory safety* below; a new unit TU gets that coverage
automatically (`MXB_UNIT_SOURCES` is shared by all three targets, no second list).

### Line coverage

`./tests/unit/coverage.sh [floor]` builds the `unit_tests_cov` target with gcov
instrumentation, runs it, and prints a per-file report via
[gcovr](https://gcovr.com/). CI runs it with a floor of `95`, a **ratchet**:
raise the floor when coverage improves, never lower it to turn a red build green.

The number is **scoped to this layer** - the production code linked into the
unit binary - and must not be quoted as a project-wide figure. For the DLL as
the integration suite drives it, `run_dll_coverage.sh` (opt-in gate
`dll-coverage`, `MXBMRP3_DLL_COVERAGE=1`) is report-only, no floor: a hit line
is not an asserted one, since render code runs under every test. The behavioral
artifact stays
[`tests/integration/API_COVERAGE.md`](tests/integration/API_COVERAGE.md), which
marks the gaps ⚪/🟠 rather than hiding them in an average.

## Layer 2 - Integration tests (`tests/integration/tests/`)

These are the heart of the suite. They **cross-compile the whole plugin to a real
Windows DLL** (mingw-w64), load it under Wine, drive the **real PiBoSo callbacks**,
and assert on the plugin's own state snapshot. This is golden-master/
characterization testing: it exercises the entire pipeline (api-export layer →
adapters → `PluginData` change detection → `buildJsonSnapshot`) and catches
*logic* regressions, not just portability breakage.

**Plugin logic is tested in isolation from the serving layer.** A logic test reads
`host.snapshot()`, which calls `buildJsonSnapshot()` **directly** (via a test hook)
- no HTTP server, no socket, no snapshot-rebuild gating. So a plugin-logic test
depends only on the plugin's computation, never on the server machinery. (An
earlier version routed everything through the live HTTP server and one test had to
fire a dummy update just to defeat the rebuild gate - accidental coupling that's
now gone.) The JSON *contract* it reads is still the plugin's own stable public
output, so asserting it isn't coupling to the overlay - the overlay is a separate
consumer with its own layer. `http_test.cpp` owns the serving path: it
starts the real server, fetches over a socket, and checks it serves exactly what
`snapshot()` builds (`http_robust_test.cpp` covers that path's survival against
hostile clients, and `http_sse_test.cpp` the streaming endpoint the overlay
actually lives on - the one path where the server holds the socket, and the
plugin's thread, open). Internal state that never reaches the snapshot (e.g. the
real-time gap) is read through its own typed hook - see *Test-only hooks* below.

```bash
./tests/integration/run_tests.sh                  # build DLL + run every tests/*.cpp
./tests/integration/run_tests.sh race sessions    # subset by basename
TEST_DEBUG=1 ./tests/integration/run_tests.sh race  # dump the driver trace on failure
MXBMRP3_TEST_TIMEOUT=30 ./tests/integration/run_tests.sh  # tighten the per-test abort cap
```

**Per-test timeout.** Each test binary runs under a wall-clock cap (default **120s**,
printed as `(cap Ns)`) so a *hung* test - a deadlock or infinite loop - is aborted with a
clear `TIMED OUT` line instead of silently burning CI minutes until the job-level cap. A
healthy test finishes in ~1–10s (the runner prints each one's elapsed time), so the default
is pure headroom. Override with `MXBMRP3_TEST_TIMEOUT=<seconds>` to tighten it locally or
loosen it for a genuinely slower run. The specialized runners have the same knob with their
own defaults: `MXBMRP3_PERF_TIMEOUT` (180s), `MXBMRP3_PERSIST_TIMEOUT` (60s),
`MXBMRP3_FUZZ_TIMEOUT` (60s/case), `MXBMRP3_CALLBACK_FUZZ_TIMEOUT` (300s).

Each `tests/*.cpp` is a self-contained doctest binary with its own plugin
lifecycle and HTTP port, run in an isolated Wine process with a clean save dir.
The runner auto-discovers every `tests/integration/tests/*.cpp`, and this table is
a full census of them - `tools/check_docs.py` (CI) fails if a test file exists
without a row here, so a new test can't land invisibly:

| Test | What it pins |
|---|---|
| `smoke_test.cpp` | lifecycle survives: Startup → DrawInit → Draw → Shutdown |
| `race_test.cpp` | standings order (from the classification array, not insertion), gaps (`Leader`/`+1.500`), best-lap formatting, an overtake re-derive, a DSQ (state + event log) |
| `sessions_test.cpp` | across practice→race→race2: race vs non-race gap semantics, a penalty, a lapped rider, the **reused-race-number stale-state** trap, and the #240 spurious-lead guard |
| `racenum_reuse_test.cpp` | **raceNum reuse inherits NOTHING** (the `PerRider<>` registry): rich per-rider state on every registered surface (gap, lap series, live-gap active bit, real-time gap, split/S-F references) → `RaceRemoveEntry` → re-add the same number → every surface reads fresh |
| `lap_test.cpp` | per-rider last lap + the **fastest-lap chip/event**: appears on a session best, moves when beaten, final-lap handling |
| `sectors_test.cpp` | best-sectors board: per-sector fastest-first rider ranking derived from lap splits (non-race) |
| `ideal_lap_test.cpp` | `idealLapMs` = the sum of a rider's **best sectors** across laps (faster than any real lap) |
| `posdelta_split_test.cpp` | `posDeltaSplit` from `RaceSplit`: positions a rider gained/lost since the last split |
| `trackpos_test.cpp` | **real-time leader gap** from `RaceTrackPosition`, read via the `MXBMRP3_Test_GetRealTimeGap` white-box hook (never in `/api/state`); tracks live; a lapped rider → gap 0 |
| `trackpos_stale_test.cpp` | a rider outside the **~10-closest** track-position batch keeps a **frozen** gap, not one recomputed from a stale position (the leader-dropout corruption) |
| `telemetry_companion_test.cpp` | **the producer half of the visibility gate**: telemetry history keeps accumulating for a HUD shown only on the OPEN companion window, and stops once no surface shows it. |
| `benchmark_companion_test.cpp` | the same gate for the **benchmark profiler's collection switch** (`bm.active`), from a real report: enabled only on the companion window, the profiler rendered its tables and never filled them. |
| `spectate_click_test.cpp` | **click-to-spectate is offered on exactly the riders it can reach**: `PluginData::isRiderSpectatable()` is the one gate for Standings / Map / Event Log / Session Charts (DNS/retired/DSQ/pitted/unknown-rider all excluded, and a pit exit re-enables it), plus the Event Log end-to-end - a row is clickable only when the event names a rider AND that rider is still reachable, so a retirement's own row (and the earlier rows about the same rider) go inert; plus **auto-hide drops the click targets with the rows** |
| `chart_sectors_test.cpp` | **Session Charts at sector resolution** (`ELEM_SECTOR_POINTS`, off by default): three completed-lap samples become nine, the LIVE in-progress lap extends the series on each RaceSplit (before any lap completes), practice stays per-lap (off-race ranking is by best lap, which has no sector analogue), one rider's broken splits fall the WHOLE field back to per-lap rather than ranking a sector against a lap, and the snapshot carries the per-sector series (plus the `sectorCount` stride) the web overlay draws from |
| `hazard_reach_test.cpp` | a **wrong-way** hazard is scanned for further ahead (`hazardWrongWayAwarenessDistance`, 250m) than a **stationary** one (`hazardAwarenessDistance`, 100m), because an oncoming rider closes the gap at roughly double the rate and the 1.5s wrong-way confirmation eats most of the warning. |
| `blueflag_test.cpp` | **blue-flag detection semantics**, via the `MXBMRP3_Test_IsRiderBlueFlagged`/`IsRiderLapping`/`RiderLappingTarget` hooks: proximity threshold, the leader/lead-lap cases, the same-lap early-out, and pit exclusion. |
| `livegaps_test.cpp` | the overlay live-gap data contract: per-rider `liveGapMs`/`liveGapValid` (valid for leader/active, false for dropped-out/lapped) - always emitted; the on/off is a client-side overlay setting |
| `overlay_snapshot_test.cpp` | the **whole** `/api/state` shape, not one field of it: the live snapshot must keep every key path and JSON type of `tests/fixtures/overlay_snapshot.json`. |
| `session_format_test.cpp` | race-**format** clock: pure-laps/time/time+laps `format` string, and the **finish-before-timer** overtime state machine (`00:00` freeze → N TO GO → FINAL LAP → CHECKERED) |
| `timing_reference_test.cpp` | Timing HUD via the `MXBMRP3_Test_Timing*` hooks: progressive reference selection (S1 → S1+S2 → whole lap, tracking the lap timer's track-position sector from the first flying lap), pit-exit timer reset, INVALID on a cut lap but not a pit out-lap, freeze on the first lap after a garage start, grid-start timing + the green-flag grace, panel height in whole grid bands |
| `spectate_test.cpp` | the camera/spectate chip follows the spectated rider through `SpectateVehicles` |
| `spectate_cameras_test.cpp` | `SpectateCameras` **wiring**: a director camera-role request posted through the real entry point is resolved to an index and written back via `*piSelect` with the "I changed it" return; the request is consumed exactly ONCE (at ~140 calls/s a sticky request would pin the camera every frame); no re-cut when already on the wanted camera; Free-Roam absent leaves the camera alone; manual-camera (Orbit/Free/Free-Roam) detection that pauses the director, including re-resolution on a camera-LIST change so a stale flag can't survive a track change. |
| `vehicle_data_test.cpp` | `RaceVehicleData` - the ONLY telemetry source while spectating/in replay (RunTelemetry is player-only). |
| `deinit_test.cpp` | `EventDeinit` / `RaceDeinit` **clear the world**: a populated 3-rider session goes empty, repopulates cleanly afterwards, and nothing survives into a DIFFERENT event that reuses a race number. |
| `run_split_test.cpp` | `RunSplit` is a **deliberate no-op** (RaceSplit owns all split handling): driving it creates no current-lap split state and leaves the splits RaceSplit computed untouched. |
| `sessionstate_test.cpp` | `RaceSessionState` green snapshots the grid; session started/ended events |
| `benchmark_registry_test.cpp` | the benchmark profiler's **registry survives a session teardown** (`PluginData::clear()` used to wipe it, leaving every report at "HUDs profiled: 0"). |
| `position_widget_test.cpp` | **PositionWidget is woken by every change that moves its readout** (Standings for the position, RaceEntries for the "/ 22" denominator) and by nothing else: it used to recompute both every frame and compare against a cached copy, which made it the most expensive widget in the plugin - 1.41us/frame with zero rebuilds - because it was the only per-frame caller of the lazily rebuilt position cache. |
| `asym_border_test.cpp` | a lone value is centred in **the card it is drawn on**, not in the content band inside it. |
| `card_anchor_sweep_test.cpp` | every HUD's content anchors to **the box that owns it** - the card for body content, the title band for the caption - never to the outer panel. |
| `crash_widget_test.cpp` | the crash widget's **streaming tally outlives every boundary that scopes the ordinary crash count**. |
| `rpm_widget_test.cpp` | the RPM strip **spans Gear + Speed, turns red exactly at the shift point** and flashes its red ones on the limiter. |
| `render_probe_test.cpp` | the **render probe emits the primitive it claims to**: `renderProbeType` 0 adds N *untextured* quads, type 1 adds none, a pinned `renderProbeSprite` is the only sprite in the frame, and an **out-of-range pin falls back to cycling rather than to sprite 0**. |
| `about_tab_test.cpp` | the **About page opens from the footer, not the tab list**, and the **Updates tag follows the available version**. |
| `whats_new_test.cpp` | the **"New" markers on settings tabs and rows appear, dismiss by their own rule, and stay dismissed**. |
| `ink_legibility_test.cpp` | **a caption drawn on a coloured slab clears the plugin's own luma threshold**, on the Notices slabs and the Gap Bar's figure over its fill. |
| `drop_shadow_test.cpp` | the **settings panel emits no shadow strings**, and the suppression is BEHAVIOUR rather than a stored preference. |
| `director_test.cpp` | auto-director **battle detection** splits two close groups at the gap break; director advisory inert by default |
| `director_lock_test.cpp` | auto-director **rider lock (hold)** release rules: the lock survives ordinary standings churn but is released when a new session (session-generation bump) resets the field |
| `director_broadcast_test.cpp` | auto-director **broadcast measurement**: replays a real tape with an injected sim-clock (from tape timestamps) so the wall-clock shot pacing plays out, then parses the director's own cut log to report cut count/rate, shot-length spread, shot-type + camera mix, and per-rider screen time - asserting it lands in a plausible broadcast band and rotates across the field (not glued to the leader). |
| `director_home_test.cpp` | **"Max shot = Off"**, the forced-rotation switch: with it off the director never cuts on a timer - it holds the broadcaster's own rider through a quiet race, still lets a story take the camera, and returns *home* when the story ends. |
| `director_events_test.cpp` | director **transparency events**: shot decisions and state changes reach the event log as Director-typed entries, state transitions carry the director button's state colors (cuts keep the per-type default), and they're emitted **unconditionally** - the in-game toggle and the overlay filter at *display* time (raw-data contract). |
| `theme_override_test.cpp` | **Per-HUD panel-theme override clears when absent**: the key is captured sparsely, so reset and entering a profile that carries no theme key must UNPIN the HUD -- while a set override still survives a save/load round trip |
| `reset_test.cpp` | **Reset All scope** (#212/#214): per-profile HUD settings revert to factory default, global sections (Rumble/Hotkeys) untouched. Per-TAB resets are `reset_tab_test.cpp`'s |
| `reset_tab_test.cpp` | **Per-tab "Reset `<tab>`" completeness**: for every settings tab, clicks every control the tab emits, presses that tab's Reset, and requires the saved INI back at factory. |
| `reset_profile_test.cpp` | per-profile **operations** on the profile-diff (`[HudName:Profile]`): active-profile / per-HUD reset scope, copy-to-all, and switch-profile persistence |
| `autoswitch_test.cpp` | **auto-by-session profile switch**: with the flag armed, the active profile follows the session type (Practice/Qualify/Race); with it off, a session change no longer overrides a manual pick |
| `pack_texture_variant_test.cpp` | a **pack HUD keeps its artwork across an upgrade**: an INI written before the pit board and gamepad pad became packs still carries `textureVariant=1`, and `applyBaseSettings` walks a section's keys in map order, so `showBackgroundTexture=1` was applied first and the variant turned it straight back off. |
| `stats_test.cpp` | player **personal-best lap** persists to the stats JSON (faster-replaces-only) + top speed and the `finiteOrZero` +Inf write guard. |
| `hotkey_capture_test.cpp` | **The hotkey-capture lockout.** Clicking a bind row arms a capture, and an armed capture deliberately swallows the whole keyboard (`HotkeyManager::update` routes into `updateCapture` and `processHotkeys` returns early) so that any key, bound or not, can be chosen. |
| `achievements_test.cpp` | **Achievements** end to end: tiers awarded at the record point with one toast each and never re-toasted; a pre-achievements stats file granted silently with ONE summary toast; `visible=0` earns but queues nothing; the rows through the real detectors |
| `messages_test.cpp` | **System messages**: hotkey toasts, the hide-all card, startup popups and their countdown, Reset's Confirm |
| `tooltip_coverage_test.cpp` | every settings row on every tab has a **tooltip with text** |
| `ui_icons_off_test.cpp` | **UI icons off** draws text |
| `settings_heavy_steps_test.cpp` | **heavy settings steps run once**: no held repeat; Spotter lists read per open |
| `hidden_input_test.cpp` | A HUD the frame is not drawing **takes no input**: the hide-all hotkey and the Widgets toggle hide at draw time and leave the visibility flags alone, which is what the input pass read, so a hidden HUD could still be right-dragged and its click targets still fired whenever the cursor was up. |
| `exploration_test.cpp` | The **exploration and hidden achievements** (`core/exploration_stats.h`) end to end: a startup notices a user pack and a styled `custom.css` and grants them silently with one summary card; Night Owl, Anniversary and the day count on the injected clock; a hand-edited settings or stats file earns its row by fingerprint; the rest each from their own real edge |
| `sprite_order_test.cpp` | the **sprite-order self-check** (`HudManager::verifySpriteRegistrationOrder`, run at the end of `setupDefaultResources`): discovery hands out absolute 1-based sprite indices and registration pushes file names in what must be the same order, two walks in different files mirroring each other's block arithmetic - a skew is silent (everything draws, with another asset's art). |
| `pb_scope_test.cpp` | the **all-time-PB notice follows the active PB scope**, not the per-bike write: under the default `PBScope::CATEGORY` the reference is the fastest lap across the whole **class**, so a first lap on a second bike in that class stores a PB for that bike yet must NOT fire the green notice unless it beats the class best (`PersonalBestUpdate::beatsScopedBest`); a first lap in a *different* class still notifies |
| `prestige_test.cpp` | **the prestige trade**: refused until the Platinum Sweep is earned (the act re-checks, so the button being drawn is not the rule), with **developer mode as a second key** that opens both the act and the badge widget; it zeroes every achievement tier and the lifetime counters they read, **keeps the personal bests** with an honest `pbCount` of 0, and the level survives the next load. |
| `preview_test.cpp` | **the positioning preview**: a HUD that draws nothing until something happens fills itself with placeholder content while its own settings tab is open, so it can be dragged into place - and a **disabled** HUD does not, since opening a tab is how you reach its switch rather than a request to see it. |
| `blocked_notice_test.cpp` | **a panel that cannot work says why, in its normal footprint**: Friends names Steam off / not available (red headline, muted hint) and keeps saying it while its settings tab is open, and Rumble names rumble off / controller missing in the graph's own box. |
| `odometer_test.cpp` | **odometer/distance accumulation**: distance integrates speed over the wall-clock gap between telemetry ticks, so the test injects the odometer clock (`MXBMRP3_Test_StatsSetNowUs`) for exact per-tick dt - accumulation is exact, the **~100m dirty-coalescing** marks dirty once then resets, a +Inf/NaN sample adds nothing (finiteOrZero), a >0.5s gap is discarded, and the total persists finite on the leave-track flush |
| `on_track_save_test.cpp` | **nothing persisted is written while riding**, not after a PB, a lifetime record or a tier earned live either, and **leaving the track writes the stats and the PB traces together**: the old mid-ride milestone save wrote the PB without its trace; crash loss is the accepted trade. |
| `fuel_stats_test.cpp` | **the fuel achievements**: litres burnt accumulate from the tank level FALLING (a refuel re-references rather than counting, and a new bike's full tank is not a burn against the last one's empty one), the burn rides the odometer's ~100m flush so an evaluation never lands on a 100Hz path, Long Walk Home is the EDGE of the tank going dry rather than every tick spent there, and Running on Fumes reads the level at the flag. |
| `proximity_test.cpp` | **Roost end to end**: the seconds come off the real `RaceTrackPosition` batch through PluginData's geometry pass into StatsManager's accumulation - not off a HUD or the spotter, both of which a player can switch off. |
| `fmx_test.cpp` | **FMX trick detection + scoring** through the real RunTelemetry path under the injectable FMX clock: hop, full-pitch BACKFLIP, landing grace, chain banking and crash-during-grace (state via `MXBMRP3_Test_FmxState`). |
| `records_parse_test.cpp` | records provider (MXB-only): canned CBR / MXB-Ranked responses through the **real parse path** (`MXBMRP3_Test_RecordsParse`) - field mapping incl. seconds→ms + date truncation, malformed/truncated/empty JSON rejected without crashing (zero records), absurd values handled sanely (multi-KB names truncated, negative/wrong-typed times, >MAX_RECORDS capped); plus the **fetch worker** via the stub seam (`MXBMRP3_Test_RecordsSetFetchStub`: sleep + canned response, no network) completing through the real thread, and **shutdown mid-fetch** pinning the join contract - `HudManager::clear()` joins the fetch thread *before* nulling the cached HUD pointers the worker touches (TimingHud) |
| `version_test.cpp` | update-checker version ordering (numeric, not lexicographic); plus the load-time **API handshake** - `GetModID`/`GetModDataVersion`/`GetInterfaceVersion` resolve under the exact export names the game looks up and answer what `mxb_api.cpp`'s static_asserts agreed to |
| `updater_test.cpp` | update install pipeline (backup→extract→verify→**rollback**) + **locked-file retry**: aborts intact when the target is held; a transient lock is recovered by the move retry |
| `settings_migration_test.cpp` | a version-mismatched INI (missing / `=4` / `=99` version line) keeps the user's HUD settings instead of silently wiping them |
| `settings_tab_test.cpp` | the settings menu **remembers its open tab**: the focused tab round-trips through save→load (by name, in `[Profiles] activeTab`), and an unknown/unavailable tab name is ignored (no empty tab) |
| `settings_sections_test.cpp` | every section `captureToCache()` produces is actually **serialized** to the INI (via `MXBMRP3_Test_CapturedSections`) - belt-and-suspenders guard on the per-HUD serializer registry (the old capture/apply/`hudOrder` "third hardcoded list" / FriendsHud silent-revert trap, now structurally one list) |
| `settings_idempotency_test.cpp` | **apply-path coverage (defaults)**: `save→load→save` is byte-identical (and a second round too), forcing `applyProfile` to read back every serialized enum/float/int/bitmask at its default and re-capture it - an asymmetric parse/clamp/format bug diverges the files |
| `settings_malformed_test.cpp` | **one malformed INI value fails alone**: keys after a value the applier cannot parse still apply; only the bad key defaults (logged), not the rest of the panel |
| `pb_gap_test.cpp` | **live gap to PB is PluginData's**: driven by the real S/F, split and lap callbacks against the central lap timer -- no reference before the first PB, then the right sign and size on the next lap |
| `lap_log_status_test.cpp` | Lap Log: a timeless lap reads PIT or INVALID, not in Digits |
| `pitboard_splits_test.cpp` | Pitboard At Splits: the hold, a repeated lap time, a pit lap |
| `gap_bar_splits_test.cpp` | Gap Bar split ticks: centerline splits or learned crossings |
| `gap_bar_auto_range_test.cpp` | Gap Bar Auto range: fits the lap's largest gap, holds it, starts over each lap |
| `pb_gap_golden_test.cpp` | **real-data golden for the live gap to PB**: a recorded grid-start race and a practice with a pit-out (`forest_short_race2_practice_gaps.tape.gz`, unthinned at the line) replayed on the recorded clock -- no gap on the grid lap, the gap at every line within 0.5 s of the official delta, no jump at any lap boundary |
| `pb_trace_persist_test.cpp` | **the all-time PB gap trace outlives the session**: a PB from one plugin lifetime is the all-time reference on the next lifetime's first flying lap, session and last-lap still empty; class scope plants the class's fastest trace on another bike, bike scope none |
| `settings_apply_values_test.cpp` | **apply-path coverage (non-defaults)**: `[Hud:Practice]` overrides carrying non-default enum/float/int values survive a load→save round-trip only if `applyProfile` applied them to the live HUD (re-captured as a sparse diff) - closes the idempotency test's default-only blind spot (`stringToX`/`validateX`/`std::stoi`) |
| `settings_defer_test.cpp` | **deferred auto-save**: `markDirty()` applies a change live but writes *nothing* to disk; `flushIfDirty()` (the leave-track flush) then writes exactly once; a flush with nothing dirty is a no-op - the "no settings write while the player is on track" contract |
| `companion_decouple_test.cpp` | **per-surface companion decoupling** on the live StandingsHud (via the `MXBMRP3_Test_Standings*` hooks): mirror-while-unconfigured → snapshot-on-first-edit (diverge) → clear-reverts-to-mirror; a diverged HUD persists its `companion*` keys through the real serializer while a configured-but-equal HUD writes **none** (upgrade-safe sparse save); per-surface render routing (game-frame suppression, companion filtering + offset, X-close fallback); and a HUD hidden in-game but shown on the companion still updates. |
| `gl_render_test.cpp` | **The in-context GL backend's actual rendered OUTPUT** (`core/hud_gl_renderer.h`) - coverage no GPU backend in this repo previously had. |
| `gamepad_layout_test.cpp` | The gamepad widget is a **picture** of a controller - frame sized from the type, ~30 button/stick offsets hand-placed against the artwork - and two references for one drawing has slid the buttons off the face twice. |
| `asset_pack_test.cpp` | An asset pack (gamepad pad, pit board) is selected **by name**, and an unknown name degrades **without forgetting**. |
| `pack_skin_test.cpp` | A pack SKIN (`base = <pack>` in the `[pack]` section) layers over its base: missing sprites and omitted geometry keys resolve from the base pack, own files and keys win - the spotter voice pack rule applied to gamepad and pit board packs, which is what makes a reskin two files instead of a copy of seventeen. |
| `gauges_migration_test.cpp` | Custom dial art drawn BEFORE gauges packs survives the upgrade. |
| `delta_trace_test.cpp` | Delta Trace: no reference plots nothing; the line covers exactly the half lap ridden, restarts with the next lap over the previous one, keeps it when riding back, holds through the line window, marks the rider (moving without a rebuild), recovers after the out-lap and a new session, shows the lap just completed in the pits, empties with a new session; no quad is wound the way the game culls |
| `map_lap_delta_test.cpp` | Map **lap delta**: Off tints nothing; On tints the stretch ridden green where gaining and red where losing, in many shades; the previous lap stays ahead of the rider; a new session drops the tint with its reference |
| `motion_test.cpp` | **Motion**: Off and every settled frame are exactly the frame drawn without it; a hidden HUD fades out from its last frame, a shown one fades in, a settings tab body fades on a switch; the Map's outline fades ahead of its fill |
| `digit_roll_test.cpp` | **Digit roll** on the gear and trip meter: steps roll, carries too, jumps and Off switch |
| `map_view_test.cpp` | Map views: zoomed, the track is cut at the map's edge and fades there (not zoomed, nothing fades); **Tilted** (zoomed only), far track is narrower; at Full range the map stays flat |
| `map_render_test.cpp` | MapHud **world-ribbon cache is transparent**: a real 2D track emits non-empty, all-finite quads in every view mode, and default-view geometry is bit-for-bit reproducible across a detail round-trip and rotate/zoom visits; the detail **20-200% dial** has real range, **adaptive** mode normalizes quad count across track lengths (fixed mode scales with length), legacy `detail=AUTO\|HIGH\|LOW` INI values migrate to scale/adaptive; a degenerate 1D track never produces a non-finite vertex |
| `theme_panel_padding_test.cpp` | **`[panel] padding-x/-y`** - the gap between a panel's border and its first glyph, which was the last unreachable number in the box model (with every border at 0 a panel still held its content 2 cells in, and no file could change it). |
| `title_band_test.cpp` | **The caption row under every `[card]` switch combination** (restored post-port; header records the two adaptations). |
| `box_terms_test.cpp` | **Every air term reaches an UNTHEMED panel.** The box model states eight terms and the settings menu documents all eight, but four - `titleMargin`, `titlePadding`, `contentMargin`, `contentPadding` - did nothing unless a theme was selected, which is not the default: `PanelBox::layoutPanel` collapsed the whole title/content BOX on "is there art to draw a border with" when only the border needs art, and `SettingsHud::cardPad*()` gated its content padding the same way, separately. |
| `ui_scale_test.cpp` | **UI scale**: drawn = own Scale x UI scale, the INI keeps own, positions untouched. |
| `center_stack_theme_test.cpp` | **The centre stack's stored-offset contract.** `[card] hud-content` must not move a panel's top edge (the Gap Bar slid when a skinner flipped it - its top was derived by subtracting a flag-gated padding from its stored offset); the Gap Bar's absolute top is one grid cell (the same root cause once computed it to y=0, top frame slice clipped, with both states agreeing); the three boxes do not overlap **unthemed** (the configuration the stored defaults are derived for); and a theme landing moves **no** top - the stored tops follow the `[Advanced]` built-in, not the theme (center_stack.h). |
| `settings_layout_test.cpp` | **The settings panel's click-region surface as behaviour**: regions are hit-tested in emission order, so a converted control that swaps its arrow pair or drops its tooltip row is silently broken while rendering perfectly. |
| `standings_layout_test.cpp` | **The plate number's two placement paths agree, and both are right.** Standings places row text via full rebuild and via the drag fast path; the plate nudge went into one path first (centred at rest, jumped high mid-drag), and the later glyph-centring move applied the offset **twice** with both paths agreeing about it - so the agreement case carries a correctness case beside it (inset < 2% of plate height, every row, tolerance an order under the ±9% failures it pins) |
| `settings_button_theme_test.cpp` | **Every settings-footer button is a themed button**: a themed button is nine quads, a flat one is one, so "did this go through `addButtonQuad`" has an integer answer. |
| `stripchart_parity_test.cpp` | **Per-primitive golden fingerprints of the four strip-chart HUDs** (Telemetry, Rumble, Performance, Session Charts): counts + position/colour/sprite/text checksums plus order-sensitive rolling hashes (z-order regressions reorder terms without moving a sum), each HUD isolated by rewriting `visible=` in the real saved INI. |
| `settings_fit_test.cpp` | **The settings panel is tall enough for every tab, and the same height on all of them.** |
| `settings_render_test.cpp` | the settings panel **draws content, and fits the screen, in all four corners of (theme on/off) x (developer mode on/off)** - the smoke test it did not have. |
| `standings_row_band_test.cpp` | **A full-row band sits inside its card, at any amount of theme air.** Reported as "the highlight grows outside the content of the card way beyond where the text is": a band's span was `contentRowInsetX()` - frame border + card border - which was the row's true inset until `[panel]` padding started acting on a plan panel's card. |
| `theme_geometry_test.cpp` | The settings panel's **theme-geometry contracts**, on the box-model surface (a previous test of this name pinned the pre-port chain; the hooks it drove stayed exported). |
| `settings_surface_test.cpp` | **The settings panel measured against the panel beside it**, which is the question every other case here cannot ask: they all measure one panel against itself, and both bugs this pins were reported from a screenshot by someone comparing neighbours. |
| `theme_palette_test.cpp` | The **three-step precedence** for colours and fonts - built-in default → theme → user override - which had no test of any kind: the Appearance tab builds its colour/font click regions BY HAND, so `MXBMRP3_Test_SettingsClickCycle` cannot reach them, and ColorConfig/FontConfig do not link into the unit suite. |
| `palette_test.cpp` | The **Appearance palette and its contract with the shipped packs**: eight of the nine pit board / gamepad skin hues are palette entries under the SAME NAMES the packs are shown by, which is what lets a player match text to their board exactly rather than by eye. |
| `pack_by_name_test.cpp` | **Every pack type names and stores itself the same way.** Two rules, both cross-type, both previously unenforced. |
| `theme_icons_test.cpp` | **Theme icon overrides follow the selected theme.** |
| `xinput_thread_test.cpp` | XInput **I/O thread**: the rumble send policy (first-send, idle-silence, transition-to-zero, disabled-guard) and 8-bit quantization survive the move off-thread - asserted on the command `setVibration()` posts, with the I/O thread stopped so it can't drain the post first |
| `settings_click_test.cpp` | the settings-menu **click path**, headless: a click routed through the real `handleClick` → hit-test `m_clickRegions` → `dispatchRegion` → `applySteppedControl` seam (`MXBMRP3_Test_SettingsClickStepped`), pinning the `SteppedControl` descriptors' clamp + hold-repeat acceleration tiers |
| `rumble_effect_test.cpp` | rumble **effect math** (the values users tune): telemetry→channel mapping through the real RunTelemetry path - zero telemetry is silent, slip ramps map correctly, a suspension spike scales by the per-bike profile JSON, airborne suppresses ground effects, malformed profile JSON falls back without crashing |
| `plugin_thread_test.cpp` | the **`[Advanced] pluginThread=1` worker thread**: every game-state callback applied on a separate thread is functionally equivalent to the sync path - the same synthetic race produces the same standings (with a `pluginThreadFlush()` barrier before asserting) |
| `plugin_thread_golden_test.cpp` | threaded twin of `replay_golden_test`: the same real full-race callback capture (the committed `*.tape.gz` fixture) reconstructs the **identical** golden result with the worker on - no event dropped, reordered, or raced across the queue |
| `plugin_thread_latency_test.cpp` | the worker's whole point: a 60 ms stall injected into `produceFrame` (via `MXBMRP3_Test_SetProduceDelayMs`) is paid by the game's Draw in sync mode but **not** in threaded mode; performance metrics stay live off-thread |
| `plugin_thread_abort_test.cpp` | worker killed by an escaping exception (via `MXBMRP3_Test_PluginThreadAbortWorker`): routing falls back inline immediately, the stranded backlog is drained in order, and threaded mode latches off (no respawn loop) |
| `plugin_thread_switch_test.cpp` | **runtime legacy↔threaded switch** (the RELOAD_CONFIG path): flip the `[Advanced] pluginThread` flag and the next Draw's `reconcileEnabled()` starts/stops the worker - standings stay correct in sync, then threaded, then sync again, on one running instance |
| `plugin_thread_teardown_test.cpp` | teardown with the worker **still running** and a callback still queued: `shutdown()` joins the worker first and drains the queue inline - clean return, no hang, no use-after-free |
| `plugin_thread_flush_test.cpp` | `flush()` **terminates** when the worker never drains (via `MXBMRP3_Test_PluginThreadSwallowBatches`, which discards batches already taken off the queue - the window where a dying worker destroys an in-flight sentinel). |
| `analytics_wiring_test.cpp` | analytics **event wiring** via the dry-run capture seam (no network): app_started is the always-sent tier (anon id + feature flags + `isDebug`); a full launch enqueues session_end + custom, a minimal launch drops both, a crash bypasses the gate |
| `analytics_identity_test.cpp` | **a corrupt analytics file is repaired, not carried for life**: a truncated file gives its id back and is rewritten (a cut-short number is not adopted), the next load is clean; a file with no id gets a new one once; whole and missing files read as before |
| `atomic_writer_test.cpp` | **the shared atomic writer**: a write lands whole with no temp left beside it; temps a killed writer left (ten minutes old, the old `<file>.tmp` too) are swept by the next write, a fresh one is left alone. |
| `http_test.cpp` | the **serving path**: the real HTTP server answers `/api/state` and it byte-matches the direct `snapshot()` |
| `http_robust_test.cpp` | slow-loris / partial / malformed clients don't wedge the server or stall the game-thread snapshot |
| `http_sse_test.cpp` | the **streaming path**: `/api/events` opens with its initial `id:`/`data:` frame, a data change is pushed with a newer sequence, and shutdown with a client attached unwinds the parked content provider promptly instead of waiting out its 15s keepalive (the DLL-detach hang class) |
| `http_gating_test.cpp` | `onDataChanged`'s two change classes: frequent types (Standings) don't rebuild the snapshot while nothing is consuming, rare transitions (RaceEntries/SessionData) rebuild anyway, and frequent types still rebuild once a client is active; plus the **build-side coalescing window**: a burst of frequent changes collapses to a couple of rebuilds rather than one each, the deferred change still lands once the window elapses, and a rare transition is never deferred |
| `replay_test.cpp` | the tape read/dispatch machinery: a `TapeWriter`-synthesized tape round-trips through `replayTape()` (no game needed) |
| `recorder_test.cpp` | the **in-plugin recorder** end-to-end: disabled (default) writes nothing; enabled, a known synthetic stream produces a well-formed `MXBHREC` tape (raw bytes asserted: magic, framing, per-type counts, the compound packings) that replays back to the same standings |
| `replay_golden_test.cpp` | **real-data golden master** (solo): replays a real 1-lap MXB Club capture, asserts the reconstructed result |
| `replay_golden_multi_test.cpp` | **real-data golden master** (24-rider Farm14 race): the whole pipeline at once - winner, time gaps, fastest-lap chip on a non-winner, a real penalty, a lapped rider, DSQ/DNS/retired |
| `teardown_test.cpp` | shutdown/unload **under load**: HTTP/SSE server live + the real 24-rider tape churning standings, then Shutdown → `FreeLibrary` (static destruction) is clean; plus the unload-**without**-Shutdown (auto-save backstop) path - guards the analytics-reported AV-on-teardown class (core + HTTP path only; Discord/Steam/records are compiled out of the test DLL) |
| `log_scrub_test.cpp` | the real log never names the user folder |
| `twitch_chat_test.cpp` | **Stream chat**, Twitch side: rendering, wrap, moderation, filters, role icons, `[StreamChat]` |
| `youtube_chat_test.cpp` | **YouTube chat**, offline: mixed order, icons, statuses, `[YouTube]` |

### Writing a new integration test

The harness (`tests/integration/harness/`) makes a test read like the scenario it
describes. A minimal one:

```cpp
#include "doctest.h"
#include "integration_main.h"   // dllPath(); main() is linked in by run_tests.sh
#include "plugin_host.h"        // loads the DLL, drives callbacks, returns JSON
#include "assertions.h"         // checkStandings / hasEvent / riderByNum

TEST_CASE("my scenario") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\myscenario\\");  // clean per-test save dir
    REQUIRE(host.startHttp());                             // start the web server via test hook

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
    });

    auto d = host.state();          // parsed /api/state (nlohmann::json)
    REQUIRE(d.is_object());
    checkStandings(d, {
        { 1, 10, "Alice", "Leader", "1:30.000" },
        { 2, 22, "Bob",   "+1.500", "1:31.000" },
    });

    host.shutdown();
}
```

Then drop the file in `tests/integration/tests/` - the runner finds it automatically
(no list to edit) and CI picks it up. Conventions that matter:

- **One plugin lifecycle per file.** The plugin is stateful (it re-derives
  standings on each update), so drive successive phases sequentially against one
  running instance and snapshot after each - don't use `SUBCASE` for phases (it
  re-enters the case body, re-running Startup).
- **Save dir** is `Z:\tmp\mxbmrp3-tests\<name>\` (the runner wipes the tree and
  pre-creates each dir). Keep it distinct so tests don't share a `settings.ini`.
- **Add a callback** the harness doesn't expose yet by adding a driving helper to
  `PluginHost` (and its struct to `plugin_api.h`, byte-compatible with
  `vendor/piboso/mxb_api.h`). **Add a JSON field** to assert by extending
  `assertions.h`. Keep the shared shape in the harness, the scenario in the test.

The harness pieces:
- `plugin_api.h` - the `SPlugins*` structs the tests drive (mirror the real ABI).
- `plugin_host.h` - `PluginHost`: `LoadLibrary` + export resolution, the callback
  drivers, `startHttp()` (via the `MXBMRP3_Test_StartHttp` hook - no settings
  seeding needed), and `state()` returning parsed JSON.
- `assertions.h` - `checkStandings()`, `hasEvent()`, `riderByNum()`.
- `integration_main.h` / `.cpp` - `dllPath()`, and the shared `main()` + doctest
  implementation, compiled once and linked into every test (it takes the DLL path
  positionally).
- `ini.h` - INI parse/diff helpers for the settings/persistence tests.
- `tape.h` - the callback-tape format (byte-identical twin of the in-plugin
  recorder) + `TapeWriter` for synthesizing tapes.
- `zipwrite.h` - in-memory zip builder (the updater test's download stand-in).
- `doctest.h` - vendored single-header framework.

### Test-only hooks

Some internal actions aren't reachable through a game callback (reset-to-defaults,
copy-profile, force a save, (re)load settings from disk, compare versions). They're
exposed as `MXBMRP3_Test_*` exports in `mxbmrp3/core/test_hooks.cpp`, gated
entirely on `MXBMRP3_TEST_BUILD` - so they **don't exist in the shipping DLL**.
Add a hook there when a test needs to invoke an internal action the game API can't
trigger.

**Hooks also cover internal state that never reaches the JSON.** Test what a
computation *is*, not just what the overlay renders. The real-time gap
(`RaceTrackPosition` → `updateRealTimeGaps`) is in-game-only - read by
`StandingsHud`, not emitted in `/api/state`. Rather than force it into the data
contract (a product decision) or leave it fuzz-only, `trackpos_test.cpp` reads it
directly via `MXBMRP3_Test_GetRealTimeGap` and asserts the algorithm (a follower's
gap is how much later it reaches a point the leader stamped). White-box, in the
plugin's own units - the right seam for internal logic the black-box snapshot
can't see.

Not every integration test asserts on `/api/state` - a settings test asserts on
the re-saved `settings.ini` instead. `reset_test.cpp` is the pattern: start the
plugin, perturb a few anchor keys in the INI on disk, pull them into live state
with the `MXBMRP3_Test_LoadSettings` hook (the "set live state" seam), run the
reset, re-save, and diff the file with `harness/ini.h`. It runs in one process -
no capture-default-in-a-separate-run dance.

Per-profile and per-tab resets are `reset_profile_test.cpp` / `reset_tab_test.cpp`.

### Real-data replay (callback tapes)

The integration tests drive **synthetic** callback streams - deterministic and
great for targeted/edge scenarios, but only as faithful as our reading of the
API. The fidelity anchor is a **callback tape**: a recording of the *real*
callbacks the game sends, replayed headlessly and asserted.

Producing and playing tapes:

- **In-plugin recorder** (`mxbmrp3/core/event_recorder.{h,cpp}`, MX Bikes only) -
  the main plugin records every callback to a binary `MXBHREC` file when a
  developer sets the hidden `[Recorder] enabled=1` INI key (no HUD, no hotkey);
  tapes land in `<save>/mxbmrp3/tapes/`. The only way to *capture* a real tape
  (needs the game). This replaces the old standalone `mxbmrp3_record.dlo` plugin,
  which used its own process + console window - closing that console `ExitProcess`ed
  the game without a clean `Shutdown()`, crashing the main plugin's teardown.
- **`tools/replay/`** - replays a tape into the plugin in **real time** (`--speed`),
  e.g. into a live plugin with the web server on so you can preview the overlay
  against real data in a browser. A manual dev/preview tool.

For **automated** testing, `PluginHost::replayTape()` reads that same format and
dispatches each event into the plugin's real exports, then a test asserts the
resulting `snapshot()` - headless, in CI, under Wine. The core users:

- `replay_test.cpp` - a round-trip on a tape synthesized with `harness/tape.h`'s
  `TapeWriter` (proves the read/dispatch machinery without needing a game).
- `recorder_test.cpp` - a full round-trip through the **in-plugin recorder**: drive
  a live race that the recorder captures to a `.tape`, then replay that tape into a
  fresh plugin instance and assert identical standings (proves the record path, not
  just replay).
- `replay_golden_test.cpp` / `replay_golden_multi_test.cpp` - the **real-data
  golden masters**: replay actual in-game captures and assert the plugin
  reconstructs the result, every value cross-checked against the session log.
  One is a solo 1-lap finish (MXB Club); the other is a **full 24-rider race**
  (Farm14) that exercises the whole pipeline at once - winner, time gaps, the
  fastest-lap chip on a non-winner, a real Cutting penalty, a lapped rider, and
  DSQ/DNS/retired states. These are the fidelity anchors for the synthetic tests.

The same tapes are reused by other tests: `plugin_thread_golden_test.cpp` replays
the solo capture with the worker thread on (identical-result equivalence),
`teardown_test.cpp` replays the 24-rider capture to load the shutdown path, and
`director_broadcast_test.cpp` replays it under an injected sim-clock via
`replayTapeTimed()`.

The captured tapes live gzipped under `tests/integration/tests/fixtures/` (recorder
format, slimmed to the state-changing events - telemetry/vehicle/draw/track-
position dropped, verified to yield the identical `/api/state` as the full
multi-megabyte captures); `tests/integration/run_tests.sh` unpacks fixtures before the run. Assert the *final*
classification + key events, not every frame - real timing is noisy.

> **Maintenance:** `harness/tape.h` must stay byte-identical to
> `mxbmrp3/core/event_recorder.{h,cpp}` (EventType values, `FileHeader`/
> `EventHeader` layout, the `RaceClassification`/`RaceTrackPosition` packings).
> **Enforced:** both files `static_assert` the same literal sizes/offsets (the
> "tape contract" blocks), so a one-sided edit fails to compile; a deliberate
> format change updates both, bumps `FileHeader::version`, and re-records.
> A recorded tape is coupled to the `mxb_api.h` struct layout at record time -
> record fresh after an API change.

## Layer 3 - Specialized runners (`tests/integration/`)

Different modalities that don't fit the snapshot-assertion shape, each its own
script:

| Runner | Kind | Asserts |
|---|---|---|
| `run_persist_test.sh` | property | flips every boolean setting, then that all survive a save→load→save round-trip (the per-HUD registry "silently reverts on restart" write-back trap) |
| `run_fuzz.sh` | survival | a corpus of malformed `settings.ini` + the six JSON config files must never crash or abort the load |
| `run_fuzz_callbacks.sh` | survival | every DLL-boundary callback survives adversarial sizes/counts/bytes (found + guards a real `TrackCenterline` OOB read) |
| `run_perf.sh` | baseline | runs two drivers against a full 50-rider grid on a long/complex ~2400m circuit (`perf_scenario.h`, a heavier superset of the real Farm14 tape): `perf_driver` (isolated per-callback cost) + `map_perf_driver` (the interleaved MAP hot loop). |
| `run_tape_bench.sh` | real-data | replays a committed `.tape.gz` (default the multiplayer Farm14 24-rider capture) through `tape_bench_driver` (PluginHost), profiles the reconstructed real field with every HUD visible, and runs `tools/benchmark_report.py` - the per-HUD **render footprint** (`default` vs `max` settings). |
| `run_installer_test.sh` | outcome | builds `packaging/mxbmrp3.nsi` with makensis, drives `Setup.exe` + the uninstaller headless under Wine, asserts the install/uninstall/registry/data-wipe mechanics (see below) |

These use `loader.cpp` (a bare, assertion-free host that just loads + runs the
plugin) rather than doctest, because they measure survival/timing over many runs,
not a single asserted outcome.

### Installer mechanics (`run_installer_test.sh`)

The one runner that tests the **packaging** rather than the plugin: it compiles
`packaging/mxbmrp3.nsi` with `makensis`, then drives the produced `Setup.exe` and
its uninstaller headless under Wine and asserts the on-disk + registry outcomes
(files laid down, `mxbmrp3_data` tree, Add/Remove keys, per-game path keys, the
`/FRESH=1` and `/UDATA=1` savepath-data wipes, partial-uninstall repoint, full
key removal, and that the HKLM write-probe leaves no stray key). It needs only
`makensis` + `wine` - not the mingw cross-build.

It drives the installer's **`/ELEVATED` command-line path** (the process the
on-demand elevation relaunch spawns), because that child takes the whole game
selection on the command line and runs the *same* install/uninstall Section,
registry and data-wipe code - so the mechanics are exercised without needing to
drive nsDialogs wizard pages headlessly.

> **Known gaps (manual Windows check).** Wine has no UAC and doesn't enforce ACLs
> for a normal user, so three things the runner can't reach stay a manual pass on
> real Windows (P1 matrix, and `packaging/mxbmrp3.nsi`): the writability probe
> actually **triggering** the elevated relaunch (Wine dirs are writable, so it
> never fires); the genuine **UAC prompt** and cross-account (standard user →
> admin credentials) elevation, including that the savepath resolves to the
> *launching* user's Documents; and the per-user **HKCU** hive branch (Wine always
> permits the HKLM write, so `useMachineReg` is always 1 here - it's the same
> `WRITE_UNINSTALL_REG` macro with a different root). The interactive pages
> themselves render correctly (verified once by hand).

## Layer 4 - Web overlay (`tests/web/`)

The browser/OBS overlay (`mxbmrp3_data/web/`) is the one piece the C++ layers
can't reach: they assert the plugin's `/api/state` JSON (what the overlay
*receives*); these assert what the overlay *draws* from it (tower ordering; battle-card
**live gaps**, reached by freeing the shared bottom slot via the localStorage
CONFIG override). They drive the overlay's built-in **`?demo` mode** - a synthetic 22-rider warmup + race that
feeds the same snapshots into `render()` the live SSE stream would - in headless
Chromium via [Playwright](https://playwright.dev), and assert the rendered DOM
(tower fills, positions are contiguous `1..N` and ascend on screen, real roster
names come through, the race phase shows `Leader` on P1, no uncaught JS errors).

```bash
./tests/web/run.sh              # install deps on first use, then run
./tests/web/run.sh --headed     # watch it drive the overlay
```

> **Local green is not proof of CI green here.** `run.sh` uses whatever Chromium
> is already installed (it cannot download in a sandbox); CI installs the
> revision `@playwright/test` pins. Those differ, and a mobile-layout assertion
> once passed locally and failed in CI on the same commit. Assert a **property**
> - does not overflow, fills the width, element is hidden - never an exact pixel,
> because pixels are a property of the browser build.

**One spec deliberately does NOT use `?demo`.** `overlay_snapshot.spec.js` loads
`tests/fixtures/overlay_snapshot.json` - captured from the real plugin by
`tests/integration/tests/overlay_snapshot_test.cpp` - and feeds it to the same
`render()` the SSE stream calls. Everything else here drives the demo, whose
snapshot the overlay writes itself, so a field renamed in `buildJsonSnapshot()`
would leave both suites green while the live overlay drew nothing. The C++ twin
fails on the rename; this one fails if the client still reads the old name.
Note the trap found while writing it: an assertion that mirrors the client's own
`fullName || name` fallback launders the drift it is meant to catch, so the spec
requires the preferred field outright.

**Lint (`tests/web/lint.sh`, the `eslint` gate).** The same Node install also
carries ESLint over every `.js` in the tree - the overlay, `sw.js` and this
suite - in about a second:

```bash
./tests/web/lint.sh             # what CI and the ctest gate run
./tests/web/lint.sh --fix       # apply the fixable ones
```

It is eslint's own `recommended` set, minus three rules the shipped overlay's
design makes unusable (`no-undef` and `no-unused-vars: vars` because the overlay's
scripts share **one global scope** and ESLint sees one file at a time; `no-redeclare`
because ES5 has no block scope). `tests/web/eslint.config.mjs` states each
reason at the rule. The gate exists because the JS had no lint at all: the two
dead-code nits fixed during the 1.28 release prep were found by hand-running
CodeQL's *code-quality* suite, which no CI job runs.

No game, no plugin, no network - just Node.js. See `tests/web/README.md` for the
gotchas (rows are `translateY`-slotted over a stable DOM order, so ranking is read
by on-screen Y; tests live outside `mxbmrp3_data/web/` because that folder ships
to users). Adding a case is one `test(...)` in `tests/web/tests/overlay.spec.js`.

## Layer 5 - Memory safety (`tests/asan/`)

Answers one question: **is the plugin corrupting memory?** Two shipped crashes
were access violations in innocent heap walks - the signature of heap corruption,
where the dump's `module+offset` shows the *victim*, never the *writer*.
AddressSanitizer instead faults **at the corrupting write**, with the writing and
allocating stacks. Two native pieces (no game, no Windows, no Wine - just
g++/clang + libasan) gate CI via the `memory-safety` job in
`.github/workflows/tests.yml`:

- **The whole unit suite under ASan + UBSan** - `ctest -R unit-asan` builds the
  same TUs with `-fsanitize=address,undefined`, so every surface the unit tests
  already exercise is checked for out-of-bounds / use-after-free / UB, not just
  for correct results. A new unit test gets this coverage automatically
  (`MXB_UNIT_SOURCES` is shared - no second list).
- **A targeted harness** (`tests/asan/memory_safety_fuzz.cpp` + `tests/asan/run.sh`)
  aimed at the fixed-buffer / index surface behind the two shipped heap-corruption
  crashes: `RaceEntryData`'s fixed buffers over hostile names/numbers, the
  leader-timing `clamp((int)(trackPos*100), 0, 99)` index over NaN/Inf/huge/random
  bit patterns, the PB gap tracker's slot input over the same domain, and churn of
  the two crash-site container types.

The **faithful** pass is the separate `memory-safety-msvc` CI job: it builds the
real plugin DLL with MSVC `/fsanitize=address` (`MXBMRP3_ASAN=ON`, `Debug` config -
exempt from the Release analytics-key requirement, so no secrets) and drives it
through the real
DLL-boundary callbacks with `callback_fuzzer.cpp` on a Windows runner, covering
the live `PluginData`/`StatsManager`/HUD/HttpServer pipeline the portable layer
can't compile. It runs automatically in the free public mirror but is **opt-in**
in the metered private repo (the `asan_msvc` checkbox on Run workflow - a Windows
runner burns minutes at 2x). `tests/asan/run_asan_msvc.ps1` reproduces it locally
on Windows.

Where a memory-safety test goes: adversarial cases for a fixed buffer or index
computation belong in `memory_safety_fuzz.cpp`; anything expressible as a normal
unit test is already covered by the `ASAN=1` rerun. Note the honest limit: ASan
catches spatial (out-of-bounds) and temporal (use-after-free / double-free)
errors on the paths actually executed - it does **not** catch pure data races.
`tests/asan/README.md` has the full policy, the MSVC ASan-runtime (`/MDd`) note,
and the no-rebuild PageHeap option for in-the-wild reproduction.

## Layer 6 - Visual (`tools/hud_window/companion_demo.sh`)

Answers the question the other five can't: **does it still look right?** The
companion window renders the plugin's live quads and strings itself, drawing text
from the game's own pre-rasterized `.fnt` atlases - the same glyph data the game
samples. So a capture is not an approximation of the in-game HUD; it is the same
primitives through the same font metrics, and it can be **pixel-diffed**.

Which renderer draws it is `[Advanced] hwAccel`, and under Wine the answer is not
the obvious one: the demo seeds no `hwAccel`, so it takes the shipped default and
**D3D11 comes up** (`hudgpu: D3D11 renderer up (4x MSAA, swapchain)` in the
capture's own log). The software rasterizer (`core/hud_sw_renderer`) draws only
with `hwAccel=0` seeded, or when D3D11 fails to start. Both consume the same
frame through the same decoders and text layout, which is why either is a fair
picture of the HUD - but they are not pixel-identical, so see the fourth way a
diff lies, below.

```bash
tools/hud_window/companion_demo.sh out.png                # default scene
tools/hud_window/companion_demo.sh out.png 25 tab Map     # a settings tab
tools/hud_window/companion_demo.sh out.png 25 tab "Lap Log"  # quote multi-word
tools/hud_window/companion_demo.sh out.png 25 gamepad     # a scene mode
tools/hud_window/companion_demo.sh --verify-deterministic 25 tab Map
SHOT_RES=2560x1440 tools/hud_window/companion_demo.sh out.png
compare -metric AE before.png after.png null:                   # 0 == no change
```

**What it is for.** A refactor that claims to preserve rendering can be *shown* to,
instead of argued to: capture the affected scenes before and after and require
`AE == 0`. That is a stronger statement than a click-region golden, which pins
emission order but not a single pixel. It is equally the way to review a change
that is *supposed* to look different - the diff is the review artifact.

**What it is not.** It is not asserted and not CI-gated: there are no committed
baselines, so it proves nothing on its own the way `ctest` does. Treat it as an
instrument (like `run_tape_bench.sh`), and keep the baseline you diff against in
the same session - a capture from a different toolchain or font revision is not a
valid baseline.

**Four ways a diff lies, all of them quiet** - the reason this section is longer
than the tool:

- **A capture rendering WITHOUT assets is the quiet failure.** Icons fall back to
  `[x]`/`[ ]` text - **72,784 px** for one scene - and the result is a plausible,
  fully-formed frame that passes the blank-frame guard easily (it is nowhere near
  uniform). Blankness is the loud failure; this one looks right. The demo now
  refuses to launch if any staged asset directory is empty. If you drive the
  window some other way, stage `plugins/mxbmrp3_data` relative to the working
  directory and check it, or you are diffing the wrong renderer.

- **Read the bounding box, not the pixel count.** A count tells you nothing about
  whether a diff is your change:

  ```bash
  compare before.png after.png -compose src diff.png
  convert diff.png -fuzz 5% -trim -format '%wx%h%O\n' info:   # where, not how much
  ```

  (The 4th version component would otherwise put a floor under every cross-branch
  diff - it is stamped from `git rev-list --count HEAD`, so a branch and its base
  always differ there. `companion_demo.sh` pins it via `MXBMRP3_VER_BUILD`, so
  captures are directly comparable. Drive the window another way and that floor
  comes back, ~185 px wherever the version shows.)

- **An async repaint can make a scene undiffable, and a fixed hold does NOT save
  you.** Something background repaints over the panel title, worth ~10,654 px.
  It is *not* a function of the hold: two runs at the SAME hold differed by that
  amount - at hold 6 on one machine, at hold 12 on another - so "always use the
  same hold" only moves which machine it bites. The demo seeds `updateMode=off`
  to remove one async source; that is an improvement, **not a fix** - the repaint
  has still been observed with it seeded. Screen a scene before trusting it:

  ```bash
  tools/hud_window/companion_demo.sh --verify-deterministic 25 tab Map
  VERIFY_N=5 tools/hud_window/companion_demo.sh --verify-deterministic 25 gear
  ```

  Treat a pass as "no divergence in N runs", never as "cannot race": at N=2 this
  passed on a scene that a later capture showed differing by 10,654 px - both
  samples had landed on the same side of the race. **So when a diff comes back
  non-zero, re-capture both sides before believing it.** A stale capture that
  caught the race is indistinguishable from a rendering regression; that is
  exactly how a 10,839 px "regression" here resolved into 185 px of version
  string plus one flaky frame.
- **The two backends do not agree pixel for pixel.** D3D11 antialiases edges (4x
  MSAA) and samples bilinearly; the software path does neither. The same scene
  captured on each differed by **751,596 px** - a diff that dwarfs any change you
  are likely to be reviewing, and it says nothing about the change. So pin the
  backend across both captures rather than assuming it:

  ```bash
  EXTRA_INI=$'[Advanced]\nhwAccel=0' tools/hud_window/companion_demo.sh out.png
  grep -c 'D3D11 renderer up' /tmp/mxbmrp3-tests/companion/mxbmrp3/mxbmrp3_log.txt
  ```

  A machine where D3D11 will not start under Wine silently captures the software
  path, so a diff between two machines can be this and nothing else.
- **Multi-word tab names must stay quoted** through to the exe
  (`... 6 tab "Lap Log"`). Unquoted, the extra word is dropped, the tab silently
  falls back to **General**, and you diff the wrong panel - reporting `AE=0` as a
  pass for a tab you never rendered.
- **A blank capture used to report success.** `import` exits 0 on an all-black
  grab, so a window that never mapped produced `==> wrote out.png` and a
  0-pixel diff against another blank. The script now requires the frame to have
  non-trivial standard deviation, retries the racy mapping, and on failure
  deletes the file and exits non-zero rather than leaving a blank for someone to
  diff against.

Needs the `screenshot` dep group (`./tools/install_deps.sh screenshot` - Xvfb +
ImageMagick) on top of the mingw/wine toolchain. `tools/hud_window/README.md`
documents the window itself, the `.fnt` layout, and the known limitations
(input still targets the game window; the `mxbmrp3_replay --window` path does not
map under Xvfb).

## Coverage

`tests/integration/API_COVERAGE.md` is the coverage manifest - every game callback and
internal action with its status (asserted / driven / survival / untested) and the
test that covers it. It's a behavioral manifest, not a line-coverage number: the
goal is that gaps are **visible**, not that every line is hit. Update it when you
add a test.

## The cross-build itself

`tests/integration/README.md` documents the mingw build engine (incremental + parallel +
ccache) and exactly how the test DLL diverges from the shipping MSVC build (all
gated by `MXBMRP3_TEST_BUILD` / `_MSC_VER`, so the shipping build is byte-for-byte
unchanged). Manual in-game testing on Windows stays the final check for input and
anything the headless build can't exercise - rendering is no longer on that list
(Layer 6 above), though the game's own GPU path is, and so is the plugin's own
D3D11 backend: `core/hud_gpu_renderer.*`'s batching and present code needs a real
device and display (its header states the gap), so manual in-game testing is its
gate; any failure falls back to the software path, which Layer 6 covers.
