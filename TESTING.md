# Testing MXBMRP3

The shipping plugin is a Windows-only MSVC DLL, but its **logic** is portable and
tested on Linux with no game and no Windows. Everything here runs in CI
(`.github/workflows/tests.yml` - on demand via **Run workflow**, as the release
workflow's gate, and automatically on pull requests in the free public mirror; see
the workflow header for why there is no push trigger) and locally with a C++17
compiler + (for the integration layer) mingw-w64 and Wine.

There are six layers, fastest first. Reach for the cheapest one that can
exercise your change (the table in `AGENTS.md` → *Testing Discipline* maps a
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

A gate whose toolchain is absent exits 3 and CTest reports it **SKIPPED**
(`SKIP_RETURN_CODE`, CTest's own convention), so the suite is useful on a box
with nothing but `g++`. Prefer this over hand-chaining the per-layer scripts,
whose chain can outlast an automated runner's command timeout.

**A SKIP is not a pass.** That check did not run, so whatever it guards is
unverified: install the tool and get a real answer rather than reporting the
suite green with skips in it:

```bash
./tools/install_deps.sh --list        # groups -> what each provides
./tools/install_deps.sh cppcheck      # then the group you need
```

The SessionStart hook (`.claude/hooks/session-start.sh`) installs every group up
front and prints a `MISSING tools:` line naming anything it could not, so an
absent tool shows once at startup rather than as a SKIPPED line mid-run.

| Layer | Framework | Needs | Runtime | Runner |
|---|---|---|---|---|
| **Unit** - pure header logic | doctest | just `g++` (+ CMake) | ~1s run (~20s cold compile) | `ctest -R '^unit'` |
| **Integration** - real plugin, driven headless | doctest + Wine | mingw-w64, wine64 | ~2 min warm (ccache); ~5-8 min cold (full cross-build + one Wine binary per `tests/*.cpp`) | `tests/integration/run_tests.sh` |
| **Specialized** - persistence / fuzz / perf / installer | bespoke | mingw-w64, wine64, python3 | ~1–3min | `tests/integration/run_*.sh` |
| **Web overlay** - rendered DOM in a real browser | Playwright | Node.js | ~40s | `tests/web/run.sh` (see the browser caveat below); `tests/web/lint.sh` for the eslint gate |
| **Memory safety** - ASan/UBSan over the portable memory surface | doctest + a targeted harness | g++/clang, libasan | ~seconds | `ctest -R unit-asan` + `tests/asan/run.sh` |

These gates sit outside the layers because they cover a *tool*, not the plugin.
`check_docs.py` fails on a CI step that isn't also a gate, so `ctest` can't be
greener than CI:

| Gate | What it covers |
|---|---|
| `fontgen` | `tools/fontgen/test.sh` - the generator matches PiBoSo's fontgen on a baseline, and every shipped `.fnt` regenerates byte for byte with tabular digits |
| `outlines2ttf` | `tools/fontgen/test_outlines2ttf.sh` - the outline-sheet importer (`outlines2ttf.py`: artwork `.ai`/`.pdf`/`.svg` -> `.ttf`, then fontgen -> `.fnt`). |
| `themeslice` | `tools/themeslice/themeslice.py --selftest` - the theme slicer's **round trip**: cut a synthetic asymmetric master into 27 slices and require each to equal its region of the source (symmetric input cannot detect a transposed cell, which is the failure that ships two corners wrong). |
| `analytics-selftest` | `tools/analytics_report.py --selftest`. Needs pandas + markdown-it-py (`./tools/install_deps.sh analytics`), else SKIPs |
| `icon-repro` | `tests/integration/check_icon_reproducibility.sh` - regenerates every shipped `mxbmrp3_data/icons/*.tga` from `assets/icons/*.svg` under the **pinned** cairosvg and requires a byte match, both set directions included. |
| `analytics-fetch-selftest` | `tools/analytics_fetch.py --selftest` - the Aptabase fetcher's rejections. |

Alongside the test layers, CI also runs **cppcheck** static analysis
(`.github/workflows/tests.yml`, over `mxbmrp3/` with vendored code excluded). It is
**blocking**: the committed baseline is at zero findings, so any new one FAILS the
build. The accepted cost is that cppcheck versions drift between runner images,
so a toolchain bump can surface a finding unrelated to your diff. To land a
legitimate one: fix it, add an inline `// cppcheck-suppress <id>` with a reason,
or - last resort - a documented entry in `.cppcheck-suppressions` (which holds
only project-wide intentional patterns). To run it locally:
**`./tests/run_cppcheck.sh`** - the same script CI invokes, so the flags can't
drift from what you reproduce (`--report` prints findings without failing). It
pins `--enable=warning`, the one severity the suppressions are curated for: a
wider `--enable` surfaces classes CI doesn't gate, not holes in the
suppressions (the script's header has the detail).

CI also runs the **enforced invariant checks**, which likewise FAIL the build.
Each script's header documents the invariant, why it is a lint, and its
escape-hatch annotation; AGENTS.md → *Maintenance Invariants* maps rule →
enforcement. A `--self-test` flag, where a script has one, is its own gate.

- `tests/integration/check_game_configs.sh` - GPB/KRP syntax
- `check_visibility_gates.sh` - HUD `isVisibleAnySurface()` gates
- `check_card_anchor_coverage.sh` - every user of the card-box accessors is swept by `card_anchor_sweep_test`
- `check_title_tier.sh` - a full HUD's caption asks for, and is measured at, `TitleTier::Large`
- `check_lazy_module_imports.sh` - the cross-built DLL imports none of `opengl32`/`d3d11`/`d3dcompiler`/`dcomp` (all resolved at runtime, so the GPU -> software -> engine fallback has nothing for the loader to fail on)
- `check_api_guards.sh` - DLL-export exception barriers
- `check_thread_safety.sh` - no raw `std::mutex`/`lock_guard`/`unique_lock` outside `core/thread_safety.h` (the analysis cannot see them)
- `check_mt_flags.sh` - a plain value member (bool, number, string, time point) of a class owning a `std::thread` is `std::atomic`, `MXB_GUARDED_BY`, or carries `mt-plain:` (`mt-plain-block:` for a run); `--self-test` is gate `mt-flags-selftest`
- `check_banned_constructs.sh` - no `throw` of our own, no `strcpy`/`strcat`/`sprintf`, no `printf` in plugin source (test-build-only code exempt, `// banned-ok:`); `--self-test` is gate `banned-constructs-selftest`
- `check_clang_tidy.sh` - clang-tidy with only the checks in `mxbmrp3/.clang-tidy`, today `bugprone-use-after-move`, plus clang `-Wthread-safety` over annotated mutexes (see `core/thread_safety.h`) on the same parse; slow, minutes
- `check_change_consumers.sh` - every `X::onDataChanged` carries a `// change-gate:` annotation
- `check_test_hook_placement.sh` - `MXBMRP3_Test_*` lives only in `core/test_hooks*.cpp`, the files excluded from every shipping target
- `check_thread_join.sh` - every first-party `std::thread` member names its joiner on the `Shutdown()` chain (`// joined-by:`), and a HUD-owned worker's joiner runs in `HudManager::clear()` before the HUD pointers are nulled
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
codeql-action evaluates. Like cppcheck it reads the code rather than running it,
so it is analysis, not a test layer.

It exists because **`codeql.yml` can only run on the public mirror** (code
scanning needs Advanced Security, which the private repo doesn't have) and the
mirror only receives code at release time - so without a local run, the first
CodeQL scan of any change is its release.

It is **opt-in** (as is `dll-coverage`, `MXBMRP3_DLL_COVERAGE=1`): a bare `ctest`
skips it even where the CodeQL bundle is installed, because labels select rather
than exclude. `CMakeLists.txt` has the reasoning at the gate.

```bash
./tools/install_deps.sh codeql                                  # ~1 GB bundle, once
MXBMRP3_CODEQL=1 ctest --test-dir build/tests -R codeql          # via CTest
./tests/integration/run_codeql.sh                                # or directly
./tests/integration/run_codeql.sh --keep-db                      # reuse the database
```

Budget ~10–15 min: it rebuilds the plugin clean under the CodeQL extractor, then
evaluates the queries. Reach for it **before a release**, or after touching a
parser, a trust boundary, or a dependency - not in an edit-compile-test loop.

Two guards keep the result from being falsely green:

- **Empty database.** An incremental build compiles nothing the extractor can
  observe, so the database analyzes to zero findings. `CCACHE_DISABLE=1` plus
  `-B` prevents it; a missing `db-cpp/` fails loudly.
- **Partial database.** A run that extracted a fraction of the tree reports
  `no findings` and exits 0, indistinguishable from a pass. The script reads
  CodeQL's own scanned-file count and fails below a floor. Raise the floor as
  the tree grows; never lower it to make a run pass.

Accepted findings live in `tests/integration/codeql_baseline.txt` - printed as
`KNOWN` and excused, everything else fails. Entries take a source-path substring
as well as a sink, because results are keyed on the *sink*, and a sink like
`logger.cpp` is shared by every log line in the plugin. The file is currently
empty by design: prefer deleting the flow to accepting it.

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
   `http_sse_test.cpp`) exercise the serving path itself. When a test needs a
   workaround to satisfy machinery it isn't testing, that's the signal a layer is
   coupled that shouldn't be - fix the seam, don't paper over it.

4. **Synthetic tests for precision, real-data golden masters for fidelity.**
   Hand-authored callback streams are deterministic and let you construct the exact
   edge case (a reused race number, a spurious lead, a DSQ) - but they're only as
   correct as *our reading* of the API. A **real captured tape** replayed
   headlessly (`replayTape()`) is the fidelity anchor: it proves the synthetic
   inputs match what the game sends. Keep both - they catch different
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
   The mapping from "what you changed" to "which layer" lives in `AGENTS.md` →
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
   This is the same rule AGENTS.md applies to mechanism detail: when the
   catalogue carried the explanation *too*, the duplicate was the half that rotted.
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
`MXB_UNIT_SOURCES` in `tests/unit/CMakeLists.txt`; that list also compiles the production
`mxbmrp3/core/ui_config.cpp` under test. What each pins:

- `test_plugin_utils.cpp` - color/time/hex helpers in `core/plugin_utils.h` (also owns the doctest impl + `main`)
- `test_notice_priority.cpp` - `hud/notice_priority.h`, the masked-notice display-timer decision
- `test_twitch_irc.cpp` - `core/twitch_irc.h`: IRC parsing, moderation, CP1252, emotes, wrap
- `test_youtube_chat.cpp` - `core/youtube_chat.h` against `tests/fixtures/youtube/`
- `test_hold_repeat.cpp` - `core/hold_repeat.h`
- `test_cursor_idle.cpp` - `core/cursor_idle.h`
- `test_text_edit.cpp` - `core/text_edit.h`
- `test_settings_find.cpp` - `hud/settings/settings_find.h`
- `test_i18n.cpp` - `core/i18n.h`: .po, TR(), pseudo-locale, shipped packs
- `test_hotkey_names.cpp` / `test_slider_control.cpp` - hotkey names fit; slider mapping
- `test_system_messages.cpp` - `core/system_messages.h`: the toast queue and which startup message is owed
- `test_achievements.cpp` - `core/achievements.h` + `core/exploration_signals.h`: the catalogue's invariants (listed in its header), tiers, progress, formatting, and generates `docs/achievements.md`
- `test_analytics_remote_config.cpp` - the remote sampling cost lever (`parseFullSample`/`shouldSendFull`): fails open to full, deterministic 0.0/1.0 endpoints
- `test_analytics_endpoint.cpp` - App-Key → Aptabase ingest-region routing (unknown/self-hosted → "" = no send)
- `test_analytics_redact.cpp` - the install id is stripped from a logged request body.
- `test_log_scrub.cpp`, `test_module_census.cpp`, `test_os_label.cpp` - no user folder, modules, OS
- `test_analytics_identity.cpp` - recovering the install id and counters from a damaged analytics file (`core/analytics_identity.h`): whole values only, garbage refused, a whole file reads as through the parser.
- `test_analytics_theme.cpp` - the `panel_theme` label (none/shipped/custom/missing) **and** the drift guard: a shipped theme missing from `kShippedThemes` fails here
- `test_director_airtime.cpp` - the director's airtime helpers: the lull round-robin (`pickNextAirtimeNum`, keyed on race number) and the dead-air floor (`pickBaselineSubject`), so "Max shot = Off" never means dead air
- `test_session_charts_math.cpp` - race-progression chart maths in `hud/session_charts_math.h`
- `test_tooltip_length.cpp` - every settings tooltip fits the 2-line/~120-char render limit
- `test_media_app_filter.cpp` - `nowPlayingApps` matching
- `test_update_asset_select.cpp` - the updater's release-asset picker (the symbols-zip-matched-first regression)
- `test_update_version_match.cpp` - `UpdateChecker::isSameRelease`, the "is the running DLL the update we just installed?" test.
- `test_ui_config.cpp` - INI-only grid-overlay defaults + the `majorEvery` clamp
- `test_render_frame_buffer.cpp` - the plugin-worker-thread triple buffer: the producer never writes the displayed slot; `acquire()` returns the latest published frame
- `test_crash_stack_format.cpp` - the crash handler's backtrace string formatting + the whole-frame `MAX_STACK_CHARS` budget
- `pixel_text_test.cpp` - `core/pixel_text.h`, the missing-assets warning's quad alphabet.
- `test_render_batch.cpp` - the GL/D3D11 shared batcher (`core/render_batch.h`).
- `test_render_layers.cpp` - HUD stacking (`core/render_layers.h`).
- `test_gl_state_fingerprint.cpp` - the comparison half of the GL state-leak check (`core/gl_state_fingerprint.h`).
- `test_hud_sw_renderer.cpp` - golden-frame sampling of the companion window's software renderer (`core/hud_sw_renderer.cpp`): fill, per-quad alpha, white-icon tinting, `.fnt` text, the scale viewport.
- `test_render_asset_decode_mips.cpp` - the glyph-atlas mip chain (`hudassets::buildFntMips`) all three renderers sample text from.
- `test_hud_sw_assets.cpp` - malformed `.fnt`/`.tga` input to the same renderer's two binary parsers.
- `test_fmx_levels.cpp` - FMX trick levels (Big Whip, Flat Whip): every threshold, word and the name length on the large row
- `test_fmx_scoring.cpp` - FMX trick scoring (`core/fmx_manager` math), and `docs/tricks.md`, which it GENERATES from `core/fmx_types.h` and diffs against the committed copy
- `test_segment_cumulative.cpp` - cumulative custom-segment timing: a contiguous run aggregates like the official splits; on-sector identity; isolated-arc fallback
- `test_blue_flag_detect.cpp` - the blue-flag/lapping proximity core (`core/blue_flag_detect.h`): wraparound, direction, backmarker-vs-lapper eligibility, stale samples, first-lapper-wins ordering.
- `test_roost_detect.cpp` - `core/roost_detect.h`, the pairwise proximity core behind Roost.
- `test_wrong_way_detect.cpp` - `core/wrong_way_detect.h`: a repeated sample keeps the flag.
- `test_battle_groups.cpp` - battle-group partitioning (`core/battle_groups.h`) behind the director and the overlay battle panel.
- `test_lap_timer.cpp` - the display rider's live lap-timer state machine (`core/lap_timer.h`).
- `test_delta_color.cpp` - the one colour rule for a time delta (`deltaColorSlot`): ahead, behind, exactly even.
- `test_split_crossing.cpp` - `timeToCrossing` (GP Bikes' third split included) and `HoldTimer`.
- `test_pb_gap_tracker.cpp` - the live gap-to-PB engine (`core/pb_gap_tracker.h`).
- `test_field_lap_traces.cpp` - per-rider lap traces and the server best (`core/field_lap_traces.h`): line order, keep checks, faster only, erase.
- `test_hud_defaults.cpp` - a reference key's stored value: a saved Default stays Default, every reference round-trips.
- `test_lap_delta_rate.cpp` - the Map's lap delta colouring (`LapDeltaRate` in `core/lap_delta_profile.h`) plus `PluginUtils::mixColor`.
- `test_pb_trace_store.cpp` - the persisted PB tables (`core/pb_trace_store.h`): round-trip with holes, malformed input refused alone, fastest lap per key, clear.
- `test_live_gap_engine.cpp` - the live leader-relative gap core (`core/live_gap_engine.h`): direction per session format, freeze-vs-set, the sub-threshold return that keeps full grids from rebuilding every HUD, trackPos quantization, lap pruning.
- `test_marker_label.cpp` - the rider-marker label core (`hud/marker_label.h`) behind Map, Radar and Gap Bar: the text per mode, the no-position fallbacks, podium colours, and the enum values the Gap Bar saves.
- `test_settings_serde.cpp` - the enum<->string converter pairs in `core/settings_serde.h` + `core/settings_serde_hud.h`, the entire on-disk representation of every enum setting.
- `test_director_scoring.cpp` - the auto-director's story-score formulas (`core/director_scoring.h`).
- `test_director_detect.cpp` - the overtake/drop edge detectors (`core/director_detect.h`).
- `test_camera_resolve.cpp` - spectate camera-name matching and role->index resolution (`handlers/camera_resolve.h`).
- `test_standings_context_window.cpp` - the standings pagination (`hud/standings_context_window.h`): which slice(s) of the classification the table draws.
- `test_records_window.cpp` - the records table's context window (`hud/records_window.h`): which slice of the fetched records surrounds the player's own PB.
- `test_font_metrics.cpp` - `LayoutMetrics` measures and Turkish-slot ink, vs the shipped `.fnt`.
- `test_font_codepage.cpp` - `core/font_codepage.h`: slots, conversion, Find fold.
- `test_gamepad_geometry.cpp` - the gamepad widget's unit system (`hud/gamepad_geometry.h`).
- `test_asset_packs.cpp` - the SHIPPED asset packs (gamepad pads and pit boards)' inis still describe the pads they replaced.
- `test_plate_geometry.cpp` - the standings race-number plate box (`hud/plate_geometry.h`): the plate is **centred in its row**.
- `test_lap_log_plan.cpp` - the Lap Log's row planning (`hud/lap_log_plan.h`): which rows are drawn and in what order.
- `test_icon_resolve.cpp` - icon sprite resolution both ways (`core/icon_resolve.h`).
- `test_severity_ramp.cpp` - the shared gauge colour ramp (`hud/severity_ramp.h`).
- `test_radar_fade.cpp` - the radar's auto-hide fade (`hud/radar_fade.h`): how visible the radar is given who is nearby.
- `test_digit_roll.cpp` - Motion's digit roll (`hud/digit_roll.h`).
- `test_motion.cpp` - Motion's timing (`core/motion.h`): fades start settled, turn round mid-way, and step at most 50 ms a frame.
- `test_impact_vignette.cpp` - the vignette's envelope (`core/impact_vignette.h`).
- `test_peak_marker.cpp` - the shared "max marker" state machine (`hud/peak_marker.h`).
- `test_standings_gap_plan.cpp` - the standings gap-column decision (`hud/standings_gap_plan.h`): value, style and tint per cell.
- `test_standings_live_order.cpp` - Standings' live order: pass hold, stale riders.
- `test_thread_detach_grace.cpp` - the spin-then-detach teardown policy (`core/thread_detach_grace.h`).
- `test_text_wrap.cpp` - the settings tooltip box's greedy word wrap (`hud/settings/text_wrap.h`).
- `test_spotter_phrase.cpp` - the spotter's phrase composition (`core/spotter_phrase.h`): number words asserted against `tests/fixtures/spotter_number_words.txt`, the SAME fixture `spottergen-selftest` asserts, so C++ wording and the baked `num_*.wav` pass together
- `test_spotter_hazard.cpp` - the spotter's proximity/hazard cue state machine (`core/spotter_hazard.h`).
- `test_spotter_milestones.cpp` - the spotter's session-progress milestones (`core/spotter_milestones.h`).
- `test_spotter_tricks.cpp` - which tricks the spotter names and how (`core/spotter_tricks.h`); one `achievement_<id>` cue per catalogue row.
- `test_spotter_language.cpp` - the spotter's translation layer: the `say()` word census, `[Words]`, and every shipped language `spotter.ini` complete.
- `test_analytics_spotter.cpp` - the spotter label (`core/analytics_spotter.h`): the theme classifier for voice packs, off reported as `"none"`.
- `test_analytics_language.cpp` - the `language` label: the pack in effect (`"en"` for none), others `"custom"`, with the theme guard's folder walk.
- `test_spotter_tts_voice.cpp` - the in-game TTS voice picker's pure half (`core/spotter_tts_voice.h`).
- `test_spotter_pace.cpp` - the spotter's pace-report tracker (`core/spotter_pace.h`).
- `test_spotter_mix.cpp` - the spotter's wav chunk mixer (`core/spotter_mix.h`): `parseWav` is a trust boundary, so malformed chunks must reject whole (`unit-asan` gives the bounds checks teeth).
- `test_install_prefs.cpp` - the installer's analytics opt-out marker (`core/install_prefs.h`).
- `test_pack_ini_path.cpp` - the pack-ini resolution rule (`core/pack_ini_path.h`) with the filesystem replaced by a set of paths.
- `test_pack_types.cpp` - censuses `AssetManager::PACK_TYPES` (the one table both user-asset copies walk) against the shipped `mxbmrp3_data/`.
- `test_spotter_pack_census.cpp` - walks the SHIPPED pack (`mxbmrp3_data/spotters/default/spotter.ini`) against the two published namespaces, in both directions.
- `test_completion_floor.cpp` - `core/completion_floor.h`, the Sweep rows' per-metal percentages.
- `test_finish_margin.cpp` - `core/finish_margin.h`, the winner-to-runner-up margin behind Photo Finish.
- `test_fuel_estimate.cpp` - the fuel arithmetic (`core/fuel_estimate.h`), shared by the Fuel widget's readout and the spotter's warning.
- `test_spotter_vars.cpp` - the `{variable}` namespace (`core/spotter_vars.h`), which is FROZEN once packs are written against it.
- `test_spotter_stretch.cpp` - the pitch-preserving time stretch (`core/spotter_stretch.h`) behind the speed setting on the wav paths.
- `test_spotter_cue_pack.cpp` - the cue-pack format (`core/spotter_cue_pack.h`), every shared pack's contract: stable key names, tolerant parse, `_wav` path-escape rejection, `expand`
- `test_spotter_queue.cpp` - the spotter's pending-cue queue (`core/spotter_queue.h`).
- `test_cpp_js_parity.cpp` - the C++ side of `tests/fixtures/cpp_js_parity.json`, the golden file `tests/web/tests/parity.spec.js` also asserts
- `asset_path_test.cpp` - `AssetPath::renderName` (`core/asset_path.h`): the display name a discovered asset gets.
- `history_ring_test.cpp` - `HistoryRing<T, N>` (`core/history_ring.h`): a fixed-capacity rolling history, including the wrap.
- `test_frame_time_stats.cpp` - `FrameTimeStats` (`core/frame_time_stats.h`): the profiler's whole-stint frame record.
- `small_vec_test.cpp` - `SmallVec<T, N>` (`core/small_vec.h`): inline storage up to N, heap spill beyond, and the move paths.
- `test_flat_int_map.cpp` - `FlatIntSet`/`FlatIntMap` (`core/flat_int_map.h`): std set/map semantics; a refill never reallocates.
- `test_engine_cull.cpp` - `core/engine_cull.h`: only pixel-less primitives leave the engine handoff.
- `test_chart_run.cpp` - `hud/chart_run.h`: the quarter-pixel merge boundary.
- `test_gap_chart_picks.cpp` - `hud/gap_chart_picks.h`: the Dash Gap chart's four lines keep the leader at every position, fill out from the rider, and stay in classification order.
- `viewport_test.cpp` - `UiViewport::compute` (`core/ui_viewport.h`): the one centered-16:9 UI rect the companion paint loop and InputManager's cursor map share.
- `nine_slice_test.cpp` - the pure geometry of `hud/nine_slice.h`: how a slice grid divides a rect.
- `panel_box_test.cpp` - `core/panel_box.h` against the box model's golden vectors.
- `layout_ini_test.cpp` - the layout/theme ini FORMAT, one line at a time.
- `layout_metrics_test.cpp` - the layout vocabulary as data: the derived values still agree with the terms they come from.
- `grid_snap_test.cpp` - panel ORIGINS land on the snap lattice, not just the rows inside them.
- `gauge_square_test.cpp` - a gauge's dial stays circular while its box lands on the cell lattice.
- `gear_geometry_test.cpp` - the gear digit is sized from the FONT and capped by its box.
- `center_stack_test.cpp` - the three centred top panels do not overlap each other or the screen edge.

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

A logic test reads `host.snapshot()`, which calls `buildJsonSnapshot()`
**directly** via a test hook - no HTTP server, socket or rebuild gating (*Principles*
2-3); the `http_*_test.cpp` files own the serving path. The JSON it reads is the
plugin's own public contract, so asserting it isn't coupling to the overlay.

```bash
./tests/integration/run_tests.sh                  # build DLL + run every tests/*.cpp
./tests/integration/run_tests.sh race sessions    # subset by basename
TEST_DEBUG=1 ./tests/integration/run_tests.sh race  # dump the driver trace on failure
MXBMRP3_TEST_TIMEOUT=30 ./tests/integration/run_tests.sh  # tighten the per-test abort cap
```

**Per-test timeout.** Each test binary runs under a wall-clock cap (default **120s**,
printed as `(cap Ns)`), so a hung test aborts with a `TIMED OUT` line instead of
burning CI minutes; a healthy one takes ~1–10s. Override with
`MXBMRP3_TEST_TIMEOUT=<seconds>`. The specialized runners have the same knob with their
own defaults: `MXBMRP3_PERF_TIMEOUT` (180s), `MXBMRP3_PERSIST_TIMEOUT` (60s),
`MXBMRP3_FUZZ_TIMEOUT` (60s/case), `MXBMRP3_CALLBACK_FUZZ_TIMEOUT` (300s).

Each `tests/*.cpp` is a self-contained doctest binary with its own plugin
lifecycle and HTTP port, run in an isolated Wine process with a clean save dir.
The runner auto-discovers them; this table is a full census, enforced by
`tools/check_docs.py`:

| Test | What it pins |
|---|---|
| `smoke_test.cpp` | lifecycle survives: Startup → DrawInit → Draw → Shutdown |
| `race_test.cpp` | standings order (from the classification array, not insertion), gaps (`Leader`/`+1.500`), best-lap formatting, an overtake re-derive, a DSQ (state + event log) |
| `sessions_test.cpp` | across practice→race→race2: race vs non-race gap semantics, a penalty, a lapped rider, the **reused-race-number stale-state** trap, and the #240 spurious-lead guard |
| `reference_caption_test.cpp` | Title rows name the reference lap |
| `racenum_reuse_test.cpp` | **raceNum reuse inherits NOTHING** (the `PerRider<>` registry): every registered per-rider surface reads fresh after `RaceRemoveEntry` + re-add |
| `lap_test.cpp` | per-rider last lap + the **fastest-lap chip/event**: appears on a session best, moves when beaten, final-lap handling |
| `sectors_test.cpp` | best-sectors board: per-sector fastest-first rider ranking derived from lap splits (non-race) |
| `ideal_lap_test.cpp` | `idealLapMs` = the sum of a rider's **best sectors** across laps (faster than any real lap) |
| `posdelta_split_test.cpp` | `posDeltaSplit` from `RaceSplit`: positions a rider gained/lost since the last split |
| `trackpos_test.cpp` | **real-time leader gap** from `RaceTrackPosition`, read via the `MXBMRP3_Test_GetRealTimeGap` white-box hook (never in `/api/state`); tracks live; a lapped rider → gap 0 |
| `trackpos_stale_test.cpp` | a rider missing from the **latest** track-position batch keeps a **frozen** gap |
| `telemetry_companion_test.cpp` | telemetry history accumulates for a HUD shown only on the companion window, and stops once no surface shows it. |
| `benchmark_companion_test.cpp` | the same visibility gate for the benchmark profiler's collection switch. |
| `benchmark_report_test.cpp` | the profiler report: saved on leaving the track and at quit (one file per stint); a game freeze shows little plugin time. |
| `spectate_click_test.cpp` | **click-to-spectate is offered on exactly the riders it can reach**: `PluginData::isRiderSpectatable()` as the one gate for Standings / Map / Event Log / Session Charts, plus the Event Log end to end |
| `chart_sectors_test.cpp` | **Session Charts at sector resolution** (`ELEM_SECTOR_POINTS`): the live lap extends the series, practice stays per-lap, broken splits fall the whole field back to per-lap, the snapshot carries the per-sector series |
| `hazard_reach_test.cpp` | a **wrong-way** hazard is scanned for further ahead than a **stationary** one. |
| `blueflag_test.cpp` | **blue-flag detection semantics**, via the `MXBMRP3_Test_IsRiderBlueFlagged`/`IsRiderLapping`/`RiderLappingTarget` hooks: proximity threshold, the leader/lead-lap cases, the same-lap early-out, and pit exclusion. |
| `livegaps_test.cpp` | the overlay live-gap data contract: per-rider `liveGapMs`/`liveGapValid` (valid for leader/active, false for dropped-out/lapped) - always emitted; the on/off is a client-side overlay setting |
| `overlay_snapshot_test.cpp` | the **whole** `/api/state` shape: every key path and JSON type of `tests/fixtures/overlay_snapshot.json`. |
| `session_format_test.cpp` | race-**format** clock: pure-laps/time/time+laps `format` string, and the **finish-before-timer** overtime state machine (`00:00` freeze → N TO GO → FINAL LAP → CHECKERED) |
| `timing_reference_test.cpp` | Timing HUD via the `MXBMRP3_Test_Timing*` hooks: progressive reference selection, pit-exit reset, INVALID on a cut lap, grid-start timing, panel height |
| `spectate_test.cpp` | the camera/spectate chip follows the spectated rider through `SpectateVehicles` |
| `spectate_cameras_test.cpp` | `SpectateCameras` **wiring**: a director camera-role request resolves to an index, is consumed exactly ONCE, and manual-camera detection pauses the director. |
| `vehicle_data_test.cpp` | `RaceVehicleData` - the ONLY telemetry source while spectating/in replay (RunTelemetry is player-only). |
| `deinit_test.cpp` | `EventDeinit` / `RaceDeinit` **clear the world**: a populated 3-rider session goes empty, repopulates cleanly afterwards, and nothing survives into a DIFFERENT event that reuses a race number. |
| `run_split_test.cpp` | `RunSplit` is a **deliberate no-op** (RaceSplit owns all split handling): driving it creates no current-lap split state and leaves the splits RaceSplit computed untouched. |
| `sessionstate_test.cpp` | `RaceSessionState` green snapshots the grid; session started/ended events |
| `benchmark_registry_test.cpp` | the benchmark profiler's **registry survives a session teardown**. |
| `position_widget_test.cpp` | **PositionWidget is woken by every change that moves its readout** and by nothing else. |
| `asym_border_test.cpp` | a lone value is centred in **the card it is drawn on**, not in the content band inside it. |
| `card_anchor_sweep_test.cpp` | every HUD's content anchors to **the box that owns it** - the card for body content, the title band for the caption - never to the outer panel. |
| `crash_widget_test.cpp` | the crash widget's **streaming tally outlives every boundary that scopes the ordinary crash count**. |
| `rpm_widget_test.cpp` | the RPM strip **spans Gear + Speed, turns red exactly at the shift point** and flashes its red ones on the limiter. |
| `toggle_strip_test.cpp` | the Toggles strip mirrors the sidebar's switches, folds, previews and docks. |
| `render_probe_test.cpp` | the **render probe emits the primitive it claims to**, and an out-of-range pin falls back to cycling. |
| `about_tab_test.cpp` | **About opens from the footer, not a tab**; **Updates tag follows the available version**; **5 clicks: Breakout** |
| `whats_new_test.cpp` | the **"New" markers on settings tabs and rows appear, dismiss by their own rule, and stay dismissed**. |
| `ink_legibility_test.cpp` | **a caption drawn on a coloured slab clears the plugin's own luma threshold**, on the Notices slabs and the Gap Bar's figure over its fill. |
| `drop_shadow_test.cpp` | the **settings panel emits no shadow strings**, as behaviour, not a stored preference. |
| `director_test.cpp` | auto-director **battle detection** splits two close groups at the gap break; director advisory inert by default |
| `director_lock_test.cpp` | auto-director **rider lock (hold)** release rules: the lock survives ordinary standings churn but is released when a new session (session-generation bump) resets the field |
| `director_broadcast_test.cpp` | auto-director **broadcast measurement**: a real tape on an injected sim-clock, its cut log required to land in a plausible broadcast band. |
| `director_home_test.cpp` | **"Max shot = Off"**: the director never cuts on a timer, lets a story take the camera, and returns *home* after. |
| `director_events_test.cpp` | director **transparency events** reach the event log unconditionally (filtered at display time). |
| `event_log_pb_test.cpp` | player **PB log rows**: one per lap; none on a first, rival or invalid lap |
| `theme_override_test.cpp` | **a per-HUD panel-theme override clears when absent**, and a set one survives a save/load round trip |
| `reset_test.cpp` | **Reset All scope** (#212/#214): per-profile HUD settings revert to factory default, global sections (Rumble/Hotkeys) untouched |
| `reset_tab_test.cpp` | **Per-tab "Reset `<tab>`" completeness**: for every settings tab, clicks every control the tab emits, presses that tab's Reset, and requires the saved INI back at factory. |
| `settings_control_persist_test.cpp` | **Every settings control is saved**: changed alone, it must change the INI and survive a reload |
| `reset_profile_test.cpp` | per-profile **operations** on the profile-diff (`[HudName:Profile]`): active-profile / per-HUD reset scope, copy-to-all, and switch-profile persistence |
| `autoswitch_test.cpp` | **auto-by-session profile switch**: with the flag armed, the active profile follows the session type (Practice/Qualify/Race); with it off, a session change no longer overrides a manual pick |
| `pack_texture_variant_test.cpp` | a **pack HUD keeps its artwork across an upgrade** despite a stale `textureVariant=1` in the INI. |
| `stats_test.cpp` | player **personal-best lap** persists to the stats JSON (faster-replaces-only) + top speed and the `finiteOrZero` +Inf write guard. |
| `hotkey_capture_test.cpp` | **the hotkey-capture lockout** never outlives the settings menu. |
| `achievements_test.cpp` | **Achievements** end to end: tiers awarded at the record point with one toast each and never re-toasted; a pre-achievements stats file granted silently with ONE summary toast; `visible=0` earns but queues nothing; the rows through the real detectors |
| `messages_test.cpp` | **System messages**: hotkey toasts, the hide-all card, startup messages on the Dash, Reset's Confirm |
| `tooltip_coverage_test.cpp` | every settings row on every tab has a **tooltip with text** |
| `settings_find_test.cpp` | settings **Find**: the index, every page fits, a result opens its row |
| `i18n_test.cpp` | a language pack from Documents translates the drawn rows |
| `i18n_limits_test.cpp` | each settings label's **room**, measured; Pseudo fits every tab |
| `ui_icons_off_test.cpp` | **UI icons off** draws text |
| `settings_heavy_steps_test.cpp` | **heavy settings steps run once**: no held repeat; Spotter lists read per open |
| `hidden_input_test.cpp` | a HUD the frame is not drawing **takes no input**. |
| `hud_stack_test.cpp` | The last HUD dragged draws **on top**, under the menu; saved. |
| `exploration_test.cpp` | the **exploration and hidden achievements** (`core/exploration_stats.h`) end to end |
| `sprite_order_test.cpp` | the **sprite-order self-check** (`HudManager::verifySpriteRegistrationOrder`), must-catch half included. |
| `pb_scope_test.cpp` | the **all-time-PB notice follows the active PB scope** (`PersonalBestUpdate::beatsScopedBest`), not the per-bike write |
| `prestige_test.cpp` | **the prestige trade**: gated on the Platinum Sweep (or developer mode), resets tiers and counters, keeps PBs. |
| `preview_test.cpp` | **the positioning preview**: an empty HUD fills with placeholders while its tab is open - unless disabled. |
| `blocked_notice_test.cpp` | **a panel that cannot work says why, in its normal footprint** (Friends, Rumble). |
| `odometer_test.cpp` | **odometer accumulation** on an injected clock: exact integration, ~100m dirty coalescing, `finiteOrZero` on +Inf/NaN, gap discard, finite persist |
| `on_track_save_test.cpp` | **nothing persisted is written while riding**; leaving the track writes stats and PB traces together. |
| `fuel_stats_test.cpp` | **the fuel achievements**: burn from a falling tank level, flushed on the odometer's ~100m cadence, edge-triggered dry tank. |
| `proximity_test.cpp` | **Roost end to end** from the real `RaceTrackPosition` batch into StatsManager. |
| `fmx_test.cpp` | **FMX trick detection + scoring** through the real RunTelemetry path under the injectable FMX clock: hop, full-pitch BACKFLIP, landing grace, chain banking and crash-during-grace (state via `MXBMRP3_Test_FmxState`). |
| `records_parse_test.cpp` | records provider (MXB-only): canned responses through the **real parse path** (`MXBMRP3_Test_RecordsParse`), the fetch worker via a stub seam incl. **shutdown mid-fetch** (the join contract), and the category dropdown |
| `version_test.cpp` | update-checker version ordering (numeric, not lexicographic); plus the load-time **API handshake** - `GetModID`/`GetModDataVersion`/`GetInterfaceVersion` resolve under the exact export names the game looks up and answer what `mxb_api.cpp`'s static_asserts agreed to |
| `updater_test.cpp` | update install pipeline (backup→extract→verify→**rollback**) + **locked-file retry**: aborts intact when the target is held; a transient lock is recovered by the move retry |
| `settings_migration_test.cpp` | a version-mismatched INI (missing / `=4` / `=99` version line) keeps the user's HUD settings instead of silently wiping them |
| `settings_tab_test.cpp` | the settings menu **remembers its open tab**: the focused tab round-trips through save→load (by name, in `[Profiles] activeTab`), and an unknown/unavailable tab name is ignored (no empty tab) |
| `settings_sections_test.cpp` | every section `captureToCache()` produces is **serialized** to the INI - belt-and-suspenders guard on the per-HUD serializer registry |
| `settings_idempotency_test.cpp` | **apply-path coverage (defaults)**: `save→load→save` is byte-identical (and a second round too), forcing `applyProfile` to read back every serialized enum/float/int/bitmask at its default and re-capture it - an asymmetric parse/clamp/format bug diverges the files |
| `settings_malformed_test.cpp` | **one malformed INI value fails alone**: keys after a value the applier cannot parse still apply; only the bad key defaults (logged), not the rest of the panel |
| `pb_gap_test.cpp` | **live gap to PB is PluginData's**: driven by the real S/F, split and lap callbacks against the central lap timer -- no reference before the first PB, then the right sign and size on the next lap |
| `lap_log_status_test.cpp` | Lap Log: a timeless lap reads PIT or INVALID, not in Digits |
| `pitboard_splits_test.cpp` | Pitboard At Splits: the hold, a repeated lap time, a pit lap |
| `gap_bar_splits_test.cpp` | Gap Bar split ticks: centerline splits or learned crossings |
| `gap_bar_auto_range_test.cpp` | Gap Bar Auto range: fits the lap's largest gap, holds it, starts over each lap |
| `pb_gap_golden_test.cpp` | **real-data golden for the live gap to PB**: a recorded grid-start race and a practice with a pit-out (`forest_short_race2_practice_gaps.tape.gz`, unthinned at the line) replayed on the recorded clock -- no gap on the grid lap, the gap at every line within 0.5 s of the official delta, no jump at any lap boundary |
| `pb_trace_persist_test.cpp` | **the all-time PB gap trace outlives the session**: a PB from one plugin lifetime is the all-time reference on the next lifetime's first flying lap, session and last-lap still empty; class scope plants the class's fastest trace on another bike, bike scope none |
| `server_best_test.cpp` | **Server best gap reference**: a rival's traced lap is read as it in both orders at the line; an untraceable faster lap or a new session drops it |
| `settings_apply_values_test.cpp` | **apply-path coverage (non-defaults)**: non-default profile overrides survive load→save only if `applyProfile` applied them - the idempotency test's blind spot |
| `settings_defer_test.cpp` | **deferred auto-save**: `markDirty()` applies a change live but writes *nothing* to disk; `flushIfDirty()` (the leave-track flush) then writes exactly once; a flush with nothing dirty is a no-op - the "no settings write while the player is on track" contract |
| `companion_decouple_test.cpp` | **per-surface companion decoupling** (via the `MXBMRP3_Test_Standings*` hooks): mirror, diverge, revert, persist-only-diverged, render routing. |
| `gl_render_test.cpp` | the in-context GL backend's rendered OUTPUT (`core/hud_gl_renderer.h`). |
| `engine_handoff_test.cpp` | the engine cull runs only on frames the engine draws. |
| `gamepad_layout_test.cpp` | the gamepad widget's button and stick placement against the artwork. |
| `asset_pack_test.cpp` | An asset pack (gamepad pad, pit board) is selected **by name**, and an unknown name degrades **without forgetting**. |
| `pack_skin_test.cpp` | a pack SKIN (`base = <pack>`) resolves missing sprites and geometry keys from its base pack. |
| `gauges_migration_test.cpp` | Custom dial art drawn BEFORE gauges packs survives the upgrade. |
| `dash_test.cpp` | Dash: the shared notice, pages, the +/- cycle |
| `now_playing_test.cpp` | Now Playing: widget, Dash page, controls, notice |
| `default_positions_test.cpp`, `spotter_position_test.cpp` | Default spots clear of shipped-on HUDs; v11, v12 moves |
| `spotter_tricks_test.cpp` | Spotter Tricks/Achievements: Tricks off by default, named mid-air, landing, combo, crash, silent in races |
| `spotter_language_test.cpp` | Spotter follows the menus' language: shipped Swedish lines and words, runtime switch, a voice pack stays English |
| `delta_trace_test.cpp` | Delta Trace: the reference line through laps, pits and sessions; no quad wound the way the game culls |
| `map_lap_delta_test.cpp` | Map **lap delta**: Off tints nothing; On tints the stretch ridden green where gaining and red where losing, in many shades; the previous lap stays ahead of the rider; a new session drops the tint with its reference |
| `motion_test.cpp` | **Motion**: Off and settled frames are the frame drawn without it; hidden HUDs fade out, shown ones in, tab bodies on a switch; the Map's outline leads its fill |
| `impact_vignette_test.cpp` | the **impact vignette** draws only for a hard hit, on, on track |
| `glow_test.cpp` | the **glow** behind signal lights, its INI scales, live switch |
| `helmet_lens_test.cpp` | the visor tint's **lens** profile |
| `digit_roll_test.cpp` | **Digit roll** on the gear and trip meter: steps roll, carries too, jumps and Off switch |
| `map_view_test.cpp` | Map views: zoomed, the track is cut at the map's edge and fades there (not zoomed, nothing fades); **Tilted** (zoomed only), far track is narrower; at Full range the map stays flat |
| `map_render_test.cpp` | MapHud **world-ribbon cache is transparent**: finite, reproducible geometry across view modes and detail round-trips; the detail dial and adaptive mode; legacy `detail=` migration; a degenerate track |
| `theme_panel_padding_test.cpp` | **`[panel] padding-x/-y`** reaches the panel. |
| `title_band_test.cpp` | **The caption row under every `[card]` switch combination**. |
| `box_terms_test.cpp` | **every air term reaches an UNTHEMED panel**. |
| `ui_scale_test.cpp` | **UI scale**: drawn = own Scale x UI scale, the INI keeps own, positions untouched. |
| `center_stack_theme_test.cpp` | **the centre stack's stored-offset contract** (center_stack.h), themed and unthemed. |
| `settings_layout_test.cpp` | **the settings panel's click-region surface as behaviour** (emission order, arrow pairs, tooltip rows). |
| `standings_layout_test.cpp` | **the plate number's two placement paths** (full rebuild, drag fast path) agree, and both are right |
| `standings_show_mode_test.cpp` | Standings Show mode: each mode's crossings and hold |
| `settings_button_theme_test.cpp` | **every settings-footer button is a themed button** (nine quads, not one). |
| `stripchart_parity_test.cpp` | **per-primitive golden fingerprints of the four strip-chart HUDs**. |
| `settings_fit_test.cpp` | **The settings panel is tall enough for every tab, and the same height on all of them.** |
| `settings_render_test.cpp` | the settings panel **draws content and fits the screen** across theme on/off x developer mode on/off. |
| `standings_row_band_test.cpp` | **a full-row band sits inside its card**, at any amount of theme air. |
| `theme_geometry_test.cpp` | the settings panel's **theme-geometry contracts** on the box-model surface. |
| `settings_surface_test.cpp` | **the settings panel measured against the panel beside it**. |
| `theme_palette_test.cpp` | the **three-step precedence** for colours and fonts: built-in default → theme → user override. |
| `palette_test.cpp` | the **Appearance palette's contract with the shipped packs**: pack hues are palette entries under the same names. |
| `pack_by_name_test.cpp` | **every pack type names and stores itself the same way**. |
| `theme_icons_test.cpp` | **Theme icon overrides follow the selected theme.** |
| `xinput_thread_test.cpp` | XInput **I/O thread**: the rumble send policy and 8-bit quantization, asserted on what `setVibration()` posts |
| `settings_click_test.cpp` | the settings-menu **click path** through the real `handleClick` (`MXBMRP3_Test_SettingsClickStepped`): `SteppedControl` clamp + hold-repeat acceleration |
| `rumble_effect_test.cpp` | rumble **effect math** through the real RunTelemetry path, incl. per-bike profile JSON and its malformed fallback |
| `plugin_thread_test.cpp` | the **`[Advanced] pluginThread=1` worker thread**: every game-state callback applied on a separate thread is functionally equivalent to the sync path - the same synthetic race produces the same standings (with a `pluginThreadFlush()` barrier before asserting) |
| `plugin_thread_golden_test.cpp` | threaded twin of `replay_golden_test`: the same real full-race callback capture (the committed `*.tape.gz` fixture) reconstructs the **identical** golden result with the worker on - no event dropped, reordered, or raced across the queue |
| `plugin_thread_latency_test.cpp` | the worker's whole point: a 60 ms stall injected into `produceFrame` (via `MXBMRP3_Test_SetProduceDelayMs`) is paid by the game's Draw in sync mode but **not** in threaded mode; performance metrics stay live off-thread |
| `plugin_thread_abort_test.cpp` | worker killed by an escaping exception (via `MXBMRP3_Test_PluginThreadAbortWorker`): routing falls back inline immediately, the stranded backlog is drained in order, and threaded mode latches off (no respawn loop) |
| `plugin_thread_switch_test.cpp` | **runtime legacy↔threaded switch** (the RELOAD_CONFIG path): flip the `[Advanced] pluginThread` flag and the next Draw's `reconcileEnabled()` starts/stops the worker - standings stay correct in sync, then threaded, then sync again, on one running instance |
| `plugin_thread_teardown_test.cpp` | teardown with the worker **still running** and a callback still queued: `shutdown()` joins the worker first and drains the queue inline - clean return, no hang, no use-after-free |
| `plugin_thread_flush_test.cpp` | `flush()` **terminates** when the worker never drains (`MXBMRP3_Test_PluginThreadSwallowBatches`). |
| `analytics_wiring_test.cpp` | analytics **event wiring** via the dry-run capture seam (no network): app_started is the always-sent tier (anon id + feature flags + `isDebug`); a full launch enqueues session_end + custom, a minimal launch drops both, a crash bypasses the gate |
| `analytics_identity_test.cpp` | **a corrupt analytics file is repaired, not carried for life**: a truncated file gives its id back and is rewritten (a cut-short number is not adopted), the next load is clean; a file with no id gets a new one once; whole and missing files read as before |
| `atomic_writer_test.cpp` | **the shared atomic writer**: whole writes, stale temps swept, fresh ones left alone. |
| `http_test.cpp` | the **serving path**: the real HTTP server answers `/api/state` and it byte-matches the direct `snapshot()` |
| `http_robust_test.cpp` | slow-loris / partial / malformed clients don't wedge the server or stall the game-thread snapshot |
| `http_sse_test.cpp` | the **streaming path**: `/api/events` initial frame, a pushed change, and a prompt shutdown with a client attached |
| `http_gating_test.cpp` | `onDataChanged`'s two change classes (frequent types gated on clients, rare transitions always rebuilt) and the build-side coalescing window |
| `replay_test.cpp` | the tape read/dispatch machinery: a `TapeWriter`-synthesized tape round-trips through `replayTape()` (no game needed) |
| `recorder_test.cpp` | the **in-plugin recorder** end-to-end: disabled (default) writes nothing; enabled, a known synthetic stream produces a well-formed `MXBHREC` tape (raw bytes asserted: magic, framing, per-type counts, the compound packings) that replays back to the same standings |
| `replay_golden_test.cpp` | **real-data golden master** (solo): replays a real 1-lap MXB Club capture, asserts the reconstructed result |
| `replay_golden_multi_test.cpp` | **real-data golden master** (24-rider Farm14 race): the whole pipeline at once - winner, time gaps, fastest-lap chip on a non-winner, a real penalty, a lapped rider, DSQ/DNS/retired |
| `teardown_test.cpp` | shutdown/unload **under load** (HTTP/SSE live + the 24-rider tape), plus unload **without** Shutdown |
| `spotter_test.cpp` | the spotter cue pipeline through the real callbacks: race events -> enable/category gates -> composed text in the cue log (audio is not asserted: Wine has no SAPI voice) |
| `log_scrub_test.cpp`, `environment_report_test.cpp` | no user folder in the log; its PC and module lines |
| `source_notice_test.cpp` | the .dlo carries the plain-ASCII source notice, and the log repeats it |
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
exposed as `MXBMRP3_Test_*` exports in the `mxbmrp3/core/test_hooks*.cpp` files,
gated entirely on `MXBMRP3_TEST_BUILD` - so they **don't exist in the shipping DLL**
(`check_test_hook_placement.sh` keeps them there). Add a hook when a test needs to
invoke an internal action the game API can't trigger.

**Hooks also cover internal state that never reaches the JSON** (*Principles* 2).
The canonical case is the in-game-only real-time gap, which `trackpos_test.cpp`
reads via `MXBMRP3_Test_GetRealTimeGap` rather than forcing it into the data
contract.

Not every integration test asserts on `/api/state` - a settings test asserts on
the re-saved `settings.ini` instead. `reset_test.cpp` is the pattern: start the
plugin, perturb a few anchor keys in the INI on disk, pull them into live state
with the `MXBMRP3_Test_LoadSettings` hook (the "set live state" seam), run the
reset, re-save, and diff the file with `harness/ini.h`. It runs in one process -
no capture-default-in-a-separate-run dance.

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
  (needs the game).
- **`tools/replay/`** - replays a tape into the plugin in **real time** (`--speed`),
  e.g. into a live plugin with the web server on so you can preview the overlay
  against real data in a browser. A manual dev/preview tool.

For **automated** testing, `PluginHost::replayTape()` reads that same format and
dispatches each event into the plugin's real exports, then a test asserts the
resulting `snapshot()` - headless, in CI, under Wine. `replay_test.cpp` proves the
machinery on a synthesized tape, `recorder_test.cpp` the record path, and
`replay_golden_test.cpp` / `replay_golden_multi_test.cpp` are the real-data golden
masters (solo MXB Club; 24-rider Farm14), every value cross-checked against the
session log. The same tapes also drive `plugin_thread_golden_test.cpp`,
`teardown_test.cpp` and `director_broadcast_test.cpp` (the last on an injected
sim-clock via `replayTapeTimed()`).

The captured tapes live gzipped under `tests/integration/tests/fixtures/` (recorder
format, slimmed to the state-changing events - telemetry/vehicle/draw/track-
position dropped, verified to yield the identical `/api/state` as the full
multi-megabyte captures); `tests/integration/run_tests.sh` unpacks fixtures before the run. Assert the *final*
classification + key events, not every frame - real timing is noisy.

> **Maintenance:** `harness/tape.h` must stay byte-identical to
> `mxbmrp3/core/event_recorder.{h,cpp}` - **enforced** by matching `static_assert`
> blocks (AGENTS.md → *Maintenance Invariants* has the rule). A recorded tape is
> coupled to the `mxb_api.h` struct layout at record time - record fresh after an
> API change.

## Layer 3 - Specialized runners (`tests/integration/`)

Different modalities that don't fit the snapshot-assertion shape, each its own
script:

| Runner | Kind | Asserts |
|---|---|---|
| `run_persist_test.sh` | property | flips every boolean setting, then that all survive a save→load→save round-trip (the per-HUD registry "silently reverts on restart" write-back trap) |
| `run_fuzz.sh` | survival | a corpus of malformed `settings.ini` + the six JSON config files must never crash or abort the load |
| `run_fuzz_callbacks.sh` | survival | every DLL-boundary callback survives adversarial sizes/counts/bytes (found + guards a real `TrackCenterline` OOB read) |
| `run_perf.sh` | baseline | times a 50-rider grid (`perf_scenario.h`): `perf_driver` (per callback) + `map_perf_driver` (MAP loop), then the ledger |
| `run_perf_ledger.sh` | exact | busy synthetic tape, virtual clock; counters vs `perf_ledger_baseline.txt` |
| `run_tape_bench.sh` | real-data | replays a committed `.tape.gz` (default the multiplayer Farm14 24-rider capture) through `tape_bench_driver` (PluginHost), profiles the reconstructed real field with every HUD visible, and runs `tools/benchmark_report.py` - the per-HUD **render footprint** (`default` vs `max` settings). |
| `run_installer_test.sh` | outcome | builds `packaging/mxbmrp3.nsi` with makensis, drives `Setup.exe` + the uninstaller headless under Wine, asserts the install/uninstall/registry/data-wipe mechanics (see below) |

These use `loader.cpp` (a bare, assertion-free host that just loads + runs the
plugin) rather than doctest, because they measure survival/timing over many runs,
not a single asserted outcome.

### Installer mechanics (`run_installer_test.sh`)

The one runner that tests the **packaging** rather than the plugin. It asserts the
on-disk + registry outcomes (files laid down, `mxbmrp3_data` tree, Add/Remove keys, per-game path keys, the
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

**One spec deliberately does NOT use `?demo`.** `overlay_snapshot.spec.js` feeds
`tests/fixtures/overlay_snapshot.json` - captured from the real plugin by
`overlay_snapshot_test.cpp` - to the same `render()` the SSE stream calls, because
a demo snapshot the overlay writes itself cannot notice a field renamed in
`buildJsonSnapshot()`. Assert the preferred field outright: mirroring the client's
own `fullName || name` fallback launders the drift the spec exists to catch.

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
reason at the rule.

No game, no plugin, no network - just Node.js. `tests/web/README.md` has the
spec list and the DOM gotchas. Adding a case is one `test(...)` in
`tests/web/tests/overlay.spec.js`.

## Layer 5 - Memory safety (`tests/asan/`)

Answers one question: **is the plugin corrupting memory?** A crash dump's
`module+offset` shows heap corruption's *victim*; AddressSanitizer faults **at the
corrupting write**, with the writing and allocating stacks (`tests/asan/README.md`
has the crashes that motivated it). Two native pieces (no game, no Windows, no Wine - just
g++/clang + libasan) gate CI via the `memory-safety` job in
`.github/workflows/tests.yml`:

- **The whole unit suite under ASan + UBSan** - `ctest -R unit-asan` builds the
  same TUs with `-fsanitize=address,undefined`; a new unit test gets this
  coverage automatically (`MXB_UNIT_SOURCES` is shared - no second list).
- **A targeted harness** (`tests/asan/memory_safety_fuzz.cpp` + `tests/asan/run.sh`)
  over the fixed-buffer / index surface behind the two shipped heap-corruption
  crashes, driven with adversarial input.

The **faithful** pass is the separate `memory-safety-msvc` CI job: the real plugin
DLL built with MSVC `/fsanitize=address` (`MXBMRP3_ASAN=ON`, `Debug`), driven
through the real DLL-boundary callbacks by `callback_fuzzer.cpp` on a Windows
runner. It runs automatically in the free public mirror but is **opt-in** in the
metered private repo (the `asan_msvc` checkbox on Run workflow).
`tests/asan/run_asan_msvc.ps1` reproduces it locally on Windows.

Where a memory-safety test goes: adversarial cases for a fixed buffer or index
computation belong in `memory_safety_fuzz.cpp`; anything expressible as a normal
unit test is already covered by `unit-asan`. ASan catches spatial and temporal
errors on the paths actually executed - **not** data races.
`tests/asan/README.md` has what the harness covers, the MSVC ASan-runtime (`/MDd`)
note, and the no-rebuild PageHeap option for in-the-wild reproduction.

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
picture of the HUD - but they are not pixel-identical (see below).

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

**What it is not.** It is not asserted or CI-gated: with no committed baselines
it proves nothing on its own. Treat it as an
instrument (like `run_tape_bench.sh`), and keep the baseline you diff against in
the same session - a capture from a different toolchain or font revision is not a
valid baseline.

**Ways a diff lies, all of them quiet** (the stories and measurements are in
`companion_demo.sh`'s comments):

- **A capture rendering WITHOUT assets looks right.** Icons fall back to
  `[x]`/`[ ]` text and the frame passes the blank-frame guard. The demo refuses to
  launch if a staged asset directory is empty; drive the window another way and
  you must stage `plugins/mxbmrp3_data` relative to the working directory yourself.
- **Read the bounding box, not the pixel count.** A count tells you nothing about
  whether a diff is your change:

  ```bash
  compare before.png after.png -compose src diff.png
  convert diff.png -fuzz 5% -trim -format '%wx%h%O\n' info:   # where, not how much
  ```

  (`companion_demo.sh` pins the 4th version component via `MXBMRP3_VER_BUILD`, so
  a branch and its base compare directly; another driver differs wherever the
  version shows.)
- **An async repaint can make a scene undiffable, and a fixed hold does NOT save
  you.** Something background repaints over the panel title, at different holds
  on different machines; seeding `updateMode=off` removes one source, not all.
  Screen a scene before trusting it:

  ```bash
  tools/hud_window/companion_demo.sh --verify-deterministic 25 tab Map
  VERIFY_N=5 tools/hud_window/companion_demo.sh --verify-deterministic 25 gear
  ```

  A pass means "no divergence in N runs", never "cannot race". **When a diff
  comes back non-zero, re-capture both sides before believing it.**
- **The two backends do not agree pixel for pixel.** D3D11 antialiases (4x MSAA)
  and samples bilinearly; the software path does neither, so the backend
  difference dwarfs any change under review. Pin the backend across both
  captures rather than assuming it:

  ```bash
  EXTRA_INI=$'[Advanced]\nhwAccel=0' tools/hud_window/companion_demo.sh out.png
  grep -c 'D3D11 renderer up' /tmp/mxbmrp3-tests/companion/mxbmrp3/mxbmrp3_log.txt
  ```

  A machine where D3D11 will not start under Wine silently captures the software
  path, so a diff between two machines can be this and nothing else.
- **Multi-word tab names must stay quoted** through to the exe
  (`... 6 tab "Lap Log"`). Unquoted, the tab silently falls back to **General**
  and `AE=0` passes for a tab you never rendered.

Needs the `screenshot` dep group (`./tools/install_deps.sh screenshot` - Xvfb +
ImageMagick) on top of the mingw/wine toolchain. `tools/hud_window/README.md`
documents the window itself, the `.fnt` layout, and the known limitations
(input still targets the game window; the `mxbmrp3_replay --window` path does not
map under Xvfb).

## Coverage

`tests/integration/API_COVERAGE.md` is the behavioral coverage manifest
(*Principles* 7): every game callback and internal action, its status, and the
test that covers it. Update it when you add a test.

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
