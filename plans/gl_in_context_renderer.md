# Spike: GL in-context HUD renderer

**For the executing session:** read `CLAUDE.md` in full first (it is the contract
for working in this repo), then this file, then the three headers named in
*Prior art* below. Work on a new `claude/...` branch based on
`claude/overlay-renderer-crashes-qqdaju` (NOT `main`, and NOT #336 - the
rationale is at the end).

## Why this exists

The v1.29.4 Overlay Renderer draws the HUD in a separate OS window over the
game so the engine is handed zero primitives (its per-primitive draw cost is
the target - worth ~30% fps on themed HUDs, measured with RenderProbeSweep).
Field experience (2026-08-30, one long session of it) showed the approach
stands on contested ground, in two distinct ways:

1. **The compositor fights it.** Windows actively promotes a focused game OFF
   the composition path (direct scanout, MPO planes, windowed independent
   flip), and a composed overlay only works while the game stays ON it. The
   grow-one-pixel escape, the full-size composition anchor, and the
   refocus rebuild in the overlay window (core/overlay_window.cpp, since
   REMOVED) are each a countermeasure to
   one promotion flavor - and promotion behavior moves with every Windows and
   driver release. This mole respawns forever.
2. **The hook ecosystem notices us.** OBS/Medal crashed on the process's
   first-ever D3D11 swapchain present (`crash_analysis/known_game_crashes.json`
   → `obs-capture-hook-crash`, second route); Discord's overlay hook crashed
   the game handling the window resize our escape performs
   (`discord-overlay-hook-resize-crash`). Both were us perturbing a GL-only
   process in ways its hooks had never seen.

The durable observation: everything that coexists successfully in this
environment (Steam overlay, Discord overlay, RTSS, ReShade) draws **inside the
game's present, with the game's own API**. This spike tests whether we can be
one of them: issue our own batched OpenGL draws during the plugin's Draw
callback, on the engine's render thread, into the engine's framebuffer - and
hand the engine zero primitives, same as the overlay does today.

If it works, the benefit survives with none of the fragility: no window, no
promotion fight, no z-order, no capture detection needed for the HUD path, and
the HUD appears IN OBS game-capture recordings (a gap the window overlay can
never close - game capture reads the game's frames).

## Prior art to reuse (do not rebuild these)

- `mxbmrp3/core/hud_sw_renderer.h` - `hudsw::Frame`, the shared frame input
  every backend consumes. The GL backend consumes it too, unchanged.
- `mxbmrp3/core/render_asset_decode.{h,cpp}` - `.tga`/`.fnt` decoders + text
  layout, extracted precisely so backends share them (parity by construction,
  pinned by the sw renderer's golden-frame tests).
- `mxbmrp3/core/hud_gpu_renderer.cpp` - `buildBatch()`: normalized coords →
  triangles through the `UiViewport::compute` rect, run-batching by texture,
  texel × quad-color semantics (the game's texture stage - white icons take the
  quad color, quad alpha fades textures). The GL backend needs the same
  batches. STRONGLY consider extracting the batch-building into a shared
  header (`render_batch.h`) used by both backends: parity by construction AND
  it becomes unit-testable pure logic (the D3D backend's is currently pinned
  by nothing - a known gap its header documents).
- the overlay window (core/overlay_window.h, since REMOVED) + `HudManager::produceFrame`
  (`core/hud_manager_render.cpp`) - the suppression contract: the engine is
  handed an empty frame ONLY while the alternative path actually presents,
  re-checked every frame, so the user can never be left HUD-less. The GL path
  plugs into this same arbitration; do not invent a second mechanism.
- `mxbmrp3/core/hud_gpu_renderer.h`'s header comment - copy its structure for
  the new backend's header: the WHY-BESPOKE note and the honest COVERAGE
  paragraph (what is and is not testable headless).

## Phase 0 - feasibility probe (the whole spike hinges here)

Question: **is a current GL context bound on the thread that runs the Draw
callback, and is drawing there visually correct?**

- In the Draw path (see `handlers/draw_handler` / `vendor/piboso/mxb_api.cpp`),
  once per session log: `wglGetCurrentContext()`, `wglGetCurrentDC()`,
  `glGetString(GL_VERSION/GL_RENDERER)`, `glGetIntegerv(GL_CONTEXT_PROFILE_MASK)`.
  Use `GetModuleHandleW(L"opengl32.dll")` + `GetProcAddress` - the game has it
  loaded (possibly ReShade's proxy in the game dir; that is fine and even
  desirable - our calls then route exactly like the game's own).
- Then one hardcoded colored quad, drawn with the most conservative path the
  context allows, wrapped in full state save/restore (below). Verify in-game:
  quad visible, correct position through the `UiViewport` mapping, game visuals
  unaffected, menus still fine.
- **Kill criteria:** no current context at Draw time → the approach is dead;
  write up the finding in this file and stop. Context present but the engine
  draws its own UI AFTER the callback in a way that buries ours → investigate
  whether that layering is acceptable (HUD under the game's menus may even be
  correct); if not, stop and write up.

## Phase 1 - state discipline + measurement

- **State save/restore is the crux.** Every GL state bit touched must be
  restored exactly: bindings (texture, VBO/VAO, program), blend func/enable,
  depth/scissor/cull enables, viewport, matrices or shader uniforms, active
  texture unit, unpack alignment. Compat-profile `glPushAttrib`/
  `glPushClientAttrib` is acceptable for the spike if available; the real
  backend should save/restore manually (push/pop is deprecated in core and
  costs more than targeted saves). A leaked bit corrupts the game's NEXT draw
  - test by riding, not by screenshot.
- **Measure before building more:** render N batched quads in-context vs
  handing the engine the same N primitives, with RenderProbeSweep /
  BenchmarkWidget (`BENCH` lines; `tools/benchmark_report.py`). The overlay's
  claim to beat is the engine per-primitive cost; if in-context GL does not
  clearly win at realistic HUD loads (a themed frame is ~1-6k quads - get real
  numbers from `surfaceFrameStats`), stop and write up. Numbers first, then
  code.

## Phase 2 - the real backend

- New `mxbmrp3/core/hud_gl_renderer.{h,cpp}` consuming `hudsw::Frame`:
  texture cache (glGenTextures once per asset, mirroring the D3D SRV cache and
  `dropTextureCache()` live-reload contract), one vertex scratch reused per
  frame (480fps budget: no per-frame allocations - see CLAUDE.md), run-batched
  draws, straight-alpha SRC_ALPHA/INV_SRC_ALPHA over the opaque game frame (no
  premultiplication - that was a transparent-window need).
- Arbitration in `produceFrame`: a third destination alongside native and the
  overlay window. INI-gated, its own experimental key (do not overload
  `overlayInGame`; A/B between the two must stay possible). Suppression
  follows "the GL path drew THIS frame", never the wish - same invariant the
  overlay pins in overlay_window_test.cpp (since REMOVED with the feature).
- **Failure latching:** any GL error accumulation or exception → repaint via
  engine primitives next frame and latch off for the session, logged once.
  The user always has a HUD.
- Cross-build: the TU must compile under mingw (`_WIN32` guards, dynamic GL
  loading only - no import-table dependency on opengl32; CLAUDE.md forbids new
  resolvable-at-load imports for the same reason hud_gpu loads d3d11 lazily).
- Respect the repo's enforced invariants (they are all in CLAUDE.md's
  Maintenance Invariants table and enforced by `check_*.sh` in CI): thread
  annotations, change-gates, vis-gates, api-guards on any new export.

## Phase 3 - validation matrix (manual, in-game; be explicit in the PR)

- MX Bikes: windowed / borderless-fullscreen / alt-tab cycles / menus / replay.
- OBS **Game Capture actively recording**: no crash, HUD present IN the
  recording (this is the headline improvement - say so in the changelog).
- Discord overlay on; ReShade installed; Medal running. None should care  - 
  we are now indistinguishable from the game's own draws.
- KRP + GPB smoke test (`check_game_configs.sh` covers compile; behavior needs
  a human).
- Headless: unit tests for the extracted batcher; the golden-frame sw tests
  keep pinning the shared decode/layout; document the GL-execution coverage
  gap in the header exactly as `hud_gpu_renderer.h` does.

## Explicitly out of scope for the spike

- Removing or demoting the overlay window (separate decision, after field
  results; its capture-hook and geometry hardening ships regardless).
- The companion window (a real separate OS window - it keeps its D3D/software
  backends and its capture-hook fallback).
- Analytics wiring, settings-UI polish beyond the INI key.

## Why base on `claude/overlay-renderer-crashes-qqdaju`, not #336

#336 predates `render_asset_decode`, `hud_gpu_renderer` (the batching to
extract), and the produceFrame suppression mechanics - the spike's own
foundations. And the branch's crash fixes protect users of the SHIPPED
v1.29.4 prerelease no matter which renderer wins; they are not experiment
baggage. If the branch merges before the spike starts, base on `main` instead
 -  same content either way.

---

# Operating instructions (added 2026-08-30, after Phase 0 passed)

## Authority: you own Phases 1-3

Phase 0 was deliberately scoped as "probe, report, stop" because two sessions
were racing the same plan. That is over. The executing session now owns
Phases 1, 2 and 3 and should NOT stop after each step to ask. Report and pause
only at these gates:

- **After the Phase 1 measurement**, with the numbers, and only if they FAIL
  the bar below (a pass proceeds straight into Phase 2).
- **Before starting Phase 3's ecosystem matrix**, which costs Thomas several
  in-game sessions and should be planned with him.
- **Any kill criterion firing**, at any point.
- **Anything that would widen the spike** beyond this plan.

## The Phase 1 perf bar, so nobody has to adjudicate it

Measure the game-thread cost attributable to HUD rendering, native path vs
in-context GL, at realistic loads (a themed frame is ~1-6k quads; get real
counts from `surfaceFrameStats`, do not guess). Use RenderProbeSweep /
BenchmarkWidget (`BENCH` lines, `tools/benchmark_report.py`).

- **PASS**: GL recovers the majority of the engine-primitive cost the sweep
  attributes, reproducibly across runs, AND total plugin frame cost stays
  inside the 2.08 ms / 480 fps budget at maximum HUD load.
- **KILL**: the difference is within run-to-run noise, or GL's own cost pushes
  plugin frame time past that budget. Write it up here and stop - a marginal
  win does not justify a second renderer's permanent maintenance.

State the actual numbers in the write-up either way. "Faster" is not a result.

## Constraints this repo imposes that the code will not tell you

- **The game issues NO Draw callbacks while the player sits in menus.** Stated
  at the removed core/overlay_window.h, `core/companion_window.h`, and in CLAUDE.md's
  HttpServer change-gating invariant, all of which depend on it. Two
  consequences: (a) any z-order or layering test must target the game's
  IN-RACE UI, which shares frames with Draw - a test against menus measures
  nothing, because we never drew that frame; (b) a GL backend correctly draws
  nothing in menus, which matches the engine (it renders no HUD there either).
- **A GL context is current per THREAD.** The probe sits above the threaded
  early return in `PluginManager::handleDraw` for this reason; `[Advanced]
  pluginThread` moves HUD BUILD work to a worker with no context. Any backend
  inherits this: build off-thread if you like, draw on the callback thread.
- **`UiViewport::compute` is the engine's own mapping.** Phase 0 proved it at
  1680x720 (the engine's bars and ours landed identically). Reuse it directly;
  do not re-derive a mapping.
- **480 fps / 2.08 ms is a real budget**, gated by `run_perf.sh`, and
  BenchmarkWidget's warning colours are tied to it.

## Working with Thomas (the only one who can run the game)

His in-game time is the bottleneck of this whole spike - not agent compute.
So **batch it**: never hand him a single question when the build could answer
three. Before each hand-off, ask what else this same launch could settle.

Each hand-off should state: the exact INI keys and values, where to be (on
track, not in menus), what to look at, what to send back, and - most
importantly - **what result would count as a KILL**. A checklist that only
describes the happy path is not finished.

Record every in-game result in this file, under its phase. This file is the
only artifact that survives context compaction on either side; a finding that
lives only in a session transcript is a finding that will be re-derived.

---

# Phase 0 result: PASS (2026-08-30)

Run by Thomas on AMD Radeon RX 6900 XT, driver 24.5.1, MX Bikes. Probe:
`core/gl_probe.{h,cpp}`, `[Advanced] glProbe`. Every Phase 0 criterion is met.

## The gate question

> Is a current GL context bound on the thread that runs the Draw callback, and
> is drawing there visually correct?

**Yes to both.**

```
GlProbe: Draw thread=14780  opengl32 resident=1  entry points=1  context=0000000000020000
GlProbe: current DC=FFFFFFFFA00126F0  its window=00000000006B0A5C  game window=00000000006B0A5C  (same window - as expected)
GlProbe: version='4.6.0 Compatibility Profile Context 24.5.1.240502' glsl='4.60' parsed=46 profileMask=0x2
GlProbe: draw framebuffer=0 read framebuffer=0
GlProbe: viewport=[0,0 1680x720]  game client=1680x720
GlProbe: readback at (328,694) = R255 G0 B255 A0 -> PROBE QUAD IS IN THE FRAMEBUFFER
GlProbe: state restored clean (51 values compared, 0 differ, 0 GL errors)
```

1. **Context is current on the Draw thread.** Not merely present: its DC's
   window IS the game window, so we are on the game's own surface.
2. **It is a 4.6 COMPATIBILITY context** (`profileMask=0x2`), so fixed function
   is available and a GL backend is not forced into a shader pipeline. Phase 2
   should still prefer shaders, but it is a choice rather than a constraint.
3. **The default framebuffer is bound** (draw=0, read=0) - we are drawing into
   the backbuffer that gets swapped, not into an intermediate target.
4. **The viewport is the full game client**, so Draw is not nested inside some
   smaller pass.
5. **The draw survives to the present.** The readback proves it reached the
   framebuffer; the screenshot proves it reached the screen. These are separate
   claims and both are now evidenced.
6. **Nothing leaked.** 51 sampled state values identical before and after, zero
   GL errors, across every verified frame - on a real AMD driver, not just the
   Mesa/llvmpipe result the headless test gives.

## The coordinate mapping is settled, and it is ours

The run was at **1680x720 = 2.333:1**, deliberately NOT 16:9 - the only geometry
where the candidate mappings differ. `UiViewport::compute(1680,720)` gives a
pillarboxed UI rect `x=200 y=0 w=1280 h=720`, predicting both bars at x 225..430
and flush in y at 36. The log printed exactly that, and the screenshot shows the
magenta (ours, in-context GL) and cyan (the engine's, from an ordinary plugin
quad) bars at the same x range, flush.

**So the engine maps normalized HUD coordinates through the same centered-16:9
rect `UiViewport` computes.** A GL backend reuses `UiViewport::compute`
directly, and the in-game HUD will land pixel-identically to the engine path at
any aspect ratio. That was an open question and it is now closed.

## Two observations worth carrying forward

- **A game-directory opengl32 proxy is transparent to us.** The `glProbe=1` run
  loaded `D:\SteamLibrary\...\MX Bikes\OPENGL32.dll` (a proxy - ReShade or
  similar); the `glProbe=2` run loaded `C:\Windows\SYSTEM32\OPENGL32.dll`. The
  probe behaved identically through both, which is direct evidence for the
  plan's expectation that our calls route exactly like the game's own.
- **Plugin cost was unchanged** with the probe drawing every frame: the
  Performance HUD read ~409 fps avg, 0.08 ms avg CPU - the same as baseline.
  This says nothing yet about a real HUD load (one quad is not a measurement);
  it only says the probe itself is not distorting what Phase 1 will measure.

## Two loose ends, neither blocking

- **The first verified frame's readback came back (0,0,0,0)**, the two after it
  (255,0,255). Unexplained. Most likely the session's first Draw landing on a
  not-yet-rendered frame. It does not affect the verdict - the screenshot
  settles visibility - but if Phase 1 sees it again it is worth understanding
  rather than assuming.
- **Z-order against the game's OWN UI is still untested**, and it is the plan's
  second kill criterion, so it outranks Phase 1's measurements: a performance
  number for something that renders under the game's UI would be measuring
  something unshippable.

  The risk is specific. Our GL draw happens *during* the Draw callback; plugin
  primitives are drawn by the engine *after* it returns. If the game renders its
  own UI between those two points, then moving the HUD in-context moves it from
  *above* game UI to *below* it - a visible regression against today, and
  something Phase 2 would have to design around.

  **A first version of this test was wrong and is recorded here so it is not
  retried:** "move the probe bar under a game MENU and look". The game issues no
  Draw callbacks in menus - documented at the removed core/overlay_window.h,
  `core/companion_window.h` and CLAUDE.md's HttpServer invariant, and
  load-bearing for the overlay's stale-hide. No Draw means no GL draw from us,
  so in a menu there is nothing to bury; the menu would simply cover an area
  where our bar was never drawn that frame, and reading that as "the game
  buries us" would be a false kill.

  The layering question is only live where the engine's own drawing shares a
  frame with the Draw callback - the game's **in-race** UI. So the test is:
  enable MX Bikes' native HUD in its options, and park the probe bars on top of
  one of its on-track elements with `glProbeX`/`glProbeY`. The existing two-bar
  design then answers it in one look, as a three-way stack: game UI, our
  in-context GL bar (magenta), and an ordinary engine primitive (cyan). Whether
  magenta sits above or below the game element, while cyan sits above it, is
  exactly the regression risk, measured directly.

## Verdict

Phase 0 passes. The approach is feasible on this hardware and driver, the
coordinate mapping is confirmed, and the state save/restore discipline holds
against a real driver.

What is NOT yet answered is the second kill criterion above - whether the game's
own in-race UI renders between our GL draw and the engine's drawing of plugin
primitives. That is a kill criterion, so it is settled before Phase 1's numbers
are worth gathering.

The probe now carries everything needed to answer both in ONE launch, because
in-game time is the scarce resource here:
- `glProbeX`/`glProbeY` park the bars over a native game UI element.
- `glProbeQuads` + `glProbeBatch` draw N quads in-context (immediate mode, and
  one `glDrawArrays` batch), against the existing engine-side `renderProbeQuads`
  at the same N and the same shape - that comparison IS Phase 1's measurement.
- A Draw-cadence heartbeat logs every resume-after-gap with its length and every
  draw-state change, so "no callbacks in menus" stops being an inherited
  assumption and becomes evidence from the actual build - for ESC, pause, chat
  and spectate alike.

See `plans/gl_probe_manual_test.md` for the run.

## Phase 1 instrument: built, and folded into the automatic sweep

The measurement is not a hand-run comparison. `RenderProbeSweep` gained GL rows
(`glQuads`/`glBatch` on `Step`), so one press of **Run sweep** prices the same
tiny-fill load at N = 500/1000/2000/6000 through the ENGINE and then through
IN-CONTEXT GL, in the same scene, seconds apart, and writes both sides plus the
difference into one report. That is deliberate rather than convenient: this
file's own header records five internally-perfect reports of an experiment that
never happened, produced by hand-stepping. A comparison spread over minutes of
INI edits is the same failure waiting to recur, and it is the one failure mode
that cannot be spotted in the output.

The report applies the bar itself and prints `=> PASS by the plan's bar` or
`=> FAILS the plan's bar`, with the numbers either way, so the verdict is not a
judgement call routed through anyone.

**The guard that matters more than the numbers.** GL rows draw nothing unless
`glProbe=2` with a live context - and a sweep that measured nothing yields rows
costing zero, which reads as "in-context GL is free" and passes the bar with a
perfect score. The report detects that (no context current, or the probe never
drew) and replaces the whole block with `*** GL ROWS MEASURED NOTHING`, refusing
to print a verdict. Pinned in `gl_probe_test.cpp`, along with the verdict itself
in both directions and both halves of the bar failing independently.

Also verified rather than asserted: `objdump -p` over the cross-built DLL shows
no `opengl32` import (14 imports, none of opengl32/d3d11/d3dcompiler/dcomp). It
is now a standing CI gate, `check_lazy_module_imports.sh`, because the whole
fallback chain depends on there being nothing for the loader to fail on - and
`opengl32` is the trap of the four, since it exists on every Windows machine and
an accidental import would never fail loudly.

---

# Phase 1 result, part A: z-order. The kill criterion does NOT fire (2026-08-30)

Run by Thomas, native MX Bikes HUD enabled, probe bars parked bottom-right over
the game's own gear indicator and across our engine-drawn Performance HUD.

Observed stack, bottom to top:

1. the game's native **gear indicator**
2. **magenta** - our in-context GL draw
3. the **Performance HUD** - an ordinary plugin primitive, drawn by the engine
4. **cyan** - also a plugin primitive, appended to `m_quads` after
   `collectRenderData()` and therefore last in the engine's list

## What this settles

**The game's own in-race UI is drawn BEFORE the Draw callback**, so an
in-context GL draw lands above it - the same place the HUD sits today. That was
the kill criterion, and it does not fire. The spike continues.

Tested against the gear indicator specifically. Any native element drawn later
in the frame would behave differently, so this is "no regression on the element
tested", not a proof about every element the game draws.

## The other half of the stack, which is expected and not a problem

Our own engine-drawn primitives (the Performance HUD, and cyan) sit ABOVE the
GL draw, because the engine renders everything we hand it after the callback
returns. That is not a defect and does not affect Phase 2: a GL backend hands
the engine ZERO primitives, so there are no engine-drawn plugin quads to sit on
top of anything, and the HUD's internal order is preserved because we would
draw all of it ourselves, in order.

**It does impose one design constraint, and it is worth stating before Phase 2
tempts anyone:** the choice is per FRAME and all-or-nothing. A hybrid - some
HUDs in-context, others handed to the engine - would put the engine-drawn ones
on top regardless of their intended order, so registration order (which is draw
order, including the HelmetOverlayHud's deliberate first-registered/behind-
everything position) would silently stop meaning anything. The existing
suppression contract is already all-or-nothing (`m_bSuppressInGame` zeroes the
whole handoff), so Phase 2 inherits the right shape - it just must not be
"improved" into a per-HUD switch.

---

# Phase 1 result, part B: the first sweep's PASS is NOT valid (2026-08-30)

Two sweeps ran and both printed `=> PASS by the plan's bar`. **Neither number
should be used.** The reports are being re-run against a corrected build.

## What they said

| | run 1 | run 2 |
|---|---|---|
| engine | 0.774 us/quad | 0.716 us/quad |
| in-context, batched | 0.00122 us/quad | **-0.00703 us/quad** |
| recovered | 99.8% | **101.0%** |
| at 6000 quads | engine 4.65 ms | in-context **-0.042 ms** |

A negative per-quad cost and 101% recovered are not flattering results, they are
impossible ones. The raw GL rows show why: deltas of -10..+4 us against a
~1830 us baseline, at every N including 6000. That is the noise band (the
engine's own fullscreen rows swing +-15 us), divided by N.

## Root cause: the batched draw was a silent no-op

`drawMeasurementLoad`'s batched path uses **client-side vertex arrays**, and did
not unbind the game's `GL_ARRAY_BUFFER` or its VAO first. With a VBO bound,
`glVertexPointer`'s pointer is reinterpreted as a byte OFFSET INTO THAT BUFFER;
with a non-zero VAO bound, client arrays are invalid outright. Either way the
draw renders nothing - and nothing, timed at any N, is a flawless score.

The code even carried a comment justifying the omission. That reasoning was
correct for the immediate-mode probe quad it was written for, and wrong for the
client-array path added later beneath it. The data agrees precisely: the
immediate-mode rows (which ignore buffer bindings) DO show a real cost that
scales with N; only the batched rows are free.

## Fixed, and what does and does not cover it

- The batched path now saves, zeroes and restores `GL_ARRAY_BUFFER_BINDING` and
  `GL_VERTEX_ARRAY_BINDING` around the draw.
- **The probe now readbacks the measurement load itself** (`loadPainted`), re-armed
  at every change of N, so each sweep step carries its own engagement check and
  logs `PAINTED` / `DREW NOTHING`. The engine rows always had one
  (`quadsHandedOver`); the GL rows had none, which is why this passed unnoticed.
- The report refuses a verdict when any GL row drew nothing, states an upper
  BOUND rather than a value when the deltas are inside the noise floor, and
  clamps the recovered percentage - a figure over 100% is not a better result,
  it is the tell.

**Honestly stated coverage gap:** the headless test that binds a VBO and VAO and
asserts the load still paints does NOT reproduce the original bug - reverting the
fix leaves it green. Mesa/llvmpipe under Wine evidently does not enforce the
client-array-with-VBO-bound rule that the AMD driver does. So the regression is
pinned only by the in-game readback, not by CI. The test is kept because it
would catch a total breakage of the load, but it must not be mistaken for
coverage of this failure.

## The lesson worth keeping

A performance instrument's first duty is to prove the experiment happened. This
file already recorded that lesson once, in `render_probe_sweep.cpp`'s header,
about hand-stepping. The GL rows were added with a guard for "no context" and
"probe never drew" and still had no guard for "the draw executed and rendered
nothing" - which is the same failure wearing a different hat, and the one that
produces the most convincing possible result.

---

# Phase 1 result, part B (second attempt): PASS, stated as a bound

Two more sweeps on the corrected build (1.29.4.395). The engine numbers are
consistent with the first pair; the GL numbers changed shape, and the reporting
was wrong again in a smaller way that is now fixed.

| | run A (21:28) | run B (21:27) |
|---|---|---|
| engine, per tiny quad | 0.931 us | 1.008 us |
| engine at 6000 quads | 5.58 ms | 6.05 ms |
| batched GL deltas (N=500/1k/2k/6k) | +24, +2, +10, +31 us | -19, -18, -15, +1 us |
| session noise (engine fullscreen rows) | ~20 us | ~32 us |

**The GL cost is below what this instrument can resolve.** At every N, including
6000, the batched delta sits inside the session's own baseline scatter. Dividing
that by N manufactures precision that is not there - which is how run B printed
`-0.01589 us/quad` and `99.7% recovered` on a build that was supposed to have
stopped doing exactly that.

## The verdict, stated correctly

Upper bound, charging the largest observed swing entirely to the largest N:

- in-context, batched: **< 0.005 us/quad**, i.e. **< 0.03 ms for 6000 quads**
- engine: **~1.0 us/quad**, i.e. **~5.6-6.0 ms for 6000 quads**

That is a factor of ~200, far past the bar's "majority of the engine cost" and
far inside the 2.08 ms budget. **PASS** - as a bound, which is the strongest
claim the data supports and is more than enough to decide.

## What was wrong with the guard, again

The noise guard added after the first failure did not fire, for three reasons,
all mine:

1. It pooled the batched and immediate rows, so one immediate row's -25 us
   noise decided the batched verdict.
2. Its threshold was a hardcoded 20 us. This machine's baseline swings +-32 us
   between steps - visible in the engine's own fullscreen rows, which are in
   every report and which I had already cited as the noise reference.
3. It treated `glPainted == -1` ("never checked") as equivalent to "painted",
   so a guard against an unverified experiment passed when the verification
   had not run at all.

Replaced with the actual signature of a real cost: **does the delta scale with
N?** The largest-N row must exceed three times the scatter of the others before
a per-quad value is printed at all; otherwise the report states an upper bound
and says why. Never a negative value, never a percentage above 100. Pinned in
both directions - an earlier version of the scaling test compared the largest-N
row against a scatter that included itself, making the pass branch unreachable
and every result, however large, come out as "below resolution".

## Still open

Whether the VBO/VAO fix actually took effect on the AMD driver is **not**
established by these reports. The batched deltas grew from ~4 us to ~25-31 us
between builds, but the session noise grew by about as much, so the change is
not attributable. The `loadPainted` readback logs the answer directly
(`GlProbe: measurement load N=... -> readback ... : PAINTED`) and that logging
shipped in the same build these sweeps ran on - so the existing log answers it
with no re-run needed.

---

# Phase 1 result, part B (final): PASS, measured and reproduced

Build 1.29.4.396, two sweeps, plus the log that settles the open question.

## The load painted

```
GlProbe: measurement load N=500  batch=1 -> readback R254 G254 B254 : PAINTED
GlProbe: measurement load N=1000 batch=1 -> readback R254 G254 B254 : PAINTED
GlProbe: measurement load N=2000 batch=1 -> readback R254 G254 B254 : PAINTED
GlProbe: measurement load N=6000 batch=1 -> readback R254 G254 B254 : PAINTED
```

36 such lines, every N, both submission paths, and **zero** `DREW NOTHING`. The
VBO/VAO fix took effect on the AMD driver, so these timings are of a draw that
actually happened. That was the one thing standing between the numbers and a
verdict.

## The numbers, by slope

The baseline wanders tens of microseconds between steps, so `delta/N` from one
row carries that offset. Differencing the smallest and largest N cancels it -
the same "the cost is the RISE, not the level" argument this instrument already
makes about the engine, applied to itself.

| | run 06:58:51 | run 07:00:31 |
|---|---|---|
| engine | 0.9393 us/quad | 0.9364 us/quad |
| in-context, batched | 0.00636 us/quad | 0.00818 us/quad |
| GL rise across N | 35 us | 45 us |
| baseline scatter that run | 12 us | 15 us |
| at 6000 quads | engine 5.64 ms / GL 0.038 ms | engine 5.62 ms / GL 0.049 ms |

The engine figures agree to **0.3%** across runs, which is what says the method
is sound. The GL rise is 3x the baseline scatter in both runs, so it is signal,
not noise - and the two GL estimates agree to within 25% of each other while
being ~130x below the engine.

**Verdict: PASS, comfortably.** In-context GL costs roughly **1/130th** of the
engine per quad; at a 6000-quad themed frame that is ~0.04 ms against ~5.6 ms,
against a 2.08 ms budget the engine path alone already blows.

## What the reporting got wrong on the way here, and why it is worth recording

The single-row estimator put the engine at 0.71-0.74 us/quad; the slope puts it
at 0.9393/0.9364. A 25% error on the headline number of the instrument, caused
by charging the baseline's drift to one row - and invisible until two runs were
compared. The report now uses the slope for this comparison, derives its noise
floor from the fullscreen rows of the same run rather than a hardcoded constant,
and never prints a negative us/quad anywhere.

Also fixed: a 319-character message built into a 320-byte buffer, which lost its
trailing newline and ran two report lines together in the first of these two
files. The long messages are separate appends now.

## Triangulation: does 0.94 us/quad survive contact with reality?

Checked, because a 130x figure is going to become this project's headline and
the method being sound is not the same as the conclusion being right. It
reconciles, but it also corrects how the number should be read.

**The engine cost checks out.** Straight from the 07:00:31 sweep, no inference:
the same scene at 43 handed-over quads runs 1.77 ms (565 fps) and at +6000 quads
runs 7.15 ms (140 fps) - so 6000 engine quads cost 5.38 ms, or 0.896 us each,
measured end to end. Against the field observation of ~6386 quads at 156 fps
(6.41 ms), the probe predicts 7.75 ms (129 fps). Same ballpark from two
completely different directions.

**The apparent conflict with the overlay's +30% dissolves once the HUD LOAD of
each measurement is pinned down.** The overlay's field figure was taken at
~370 fps, i.e. a 2.70 ms frame. Subtract the 1.77 ms floor and that scene's HUD
was costing ~0.93 ms, about 1000 quads - not 6000. Removing it entirely would
give +53%; the overlay delivered +30%, which is the right shape, because the
overlay still has to draw the HUD somewhere (its own thread, its own GPU work,
the compositor). It relocates the cost. In-context GL removes it.

**So the correction, and it matters: 130x cheaper per quad is NOT 130x faster.**
The saving equals whatever the engine currently spends on HUD primitives, which
scales with what the player has switched on:

| HUD load | engine cost | fps gain if it goes to ~zero |
|---|---|---|
| Thomas's practice profile, ~260 quads | ~0.24 ms | ~+14% |
| a moderate HUD, ~1000 quads (the overlay's own test scene) | ~0.93 ms | ~+53% |
| a heavy themed HUD, ~6000 quads | ~5.6 ms | ~+250% |

The 130x is a per-quad ratio and belongs in the engineering write-up. The
user-facing claim is the table: **a light HUD gains a little, a heavy themed HUD
gains enormously** - and it is the heavy case that motivated the whole spike,
since a theme multiplies quads ~27x per panel.

One number in that table is inferred rather than measured: the ~260-quad figure
for the practice profile is derived from the screenshots' 497 fps against the
sweep's 1.77 ms floor, not read from a counter. `surfaceFrameStats` can report
it directly and should, on the next in-game round, so the table's left column
stops being an estimate.

---

# The buildBatch extraction (2026-08-31)

Done, under the three stated conditions. `core/render_batch.{h,cpp}` now holds
the API-agnostic half of batching and `hud_gpu_renderer.cpp` calls it.

**1. Verbatim.** The diff between the old `Impl::buildBatch` body and the new
`hudbatch::build` changes exactly four things: the signature, `Run`'s two COM
pointers becoming an opaque handle plus a `Shader` enum, the two cache lookups
becoming `Resolver` calls, and `UINT` becoming `uint32_t`. No arithmetic, no
ordering, no early-out. That is checkable by inspection, which is the whole
reason to move code mechanically.

**2. Tests as its first act.** 14 cases / 71 assertions in
`tests/unit/test_render_batch.cpp`, written and passing before the D3D backend
was rewired onto the shared code.

**3. The sharing is real.** Assessed before committing to it, per the
instruction to bail if it was not. Roughly 90% of the function - viewport
mapping, corner order, the two-triangle split, UV basis, sprite and font index
resolution, text scale/justify/layout, run coalescing - has nothing to do with
D3D. The genuinely API-specific parts are exactly two: which texture, and which
shader. Both reduce to an opaque handle, so no template and no API type leaks
into the header.

One thing made this cleaner than expected and is worth recording because it
looks wrong: **the NDC mapping is shared verbatim, not branched per API.** D3D
and GL disagree about the texture origin and the depth range, but both put clip
space y = +1 at the TOP of the viewport, so `1 - py*2/h` is correct for each.
The texture-origin difference is likewise NOT a batching concern - though the
first version of this paragraph said it was, claiming the GL backend "uploads
flipped". That was wrong, the GL backend duly did it, and the correction is
recorded under Phase 2 below. Both APIs map the first row of pixel data to v=0;
they only disagree about what to call that edge.

**The cost, which CI cannot pay.** The D3D backends have no headless coverage
(no device, no display), so the extraction needs in-game re-validation of the
companion window and of the overlay with `hwAccel` on. It is on the round-3
checklist in `plans/gl_probe_manual_test.md` rather than scheduled separately -
a two-minute look. Until that comes back, the extraction is unvalidated on the
D3D side, and the honest limit of the new tests is stated in their own header:
they characterise the batcher AFTER the move and cannot prove the move itself.

## Review findings on the extraction, both actioned

**1. The extraction created an unguarded binary-layout dependency - fixed.**
`hud_gpu_renderer.cpp`'s input-layout descriptors read `Vertex` by hardcoded
byte offset (0, 8, 16) with stride `sizeof(Vertex)`. That was low-risk while
`Vertex` was a private struct forty lines away; it stopped being low-risk the
moment `Vertex` moved into a shared header the GL backend will also edit. A
field added there for GL's benefit would shift those offsets and corrupt every
D3D draw, and nothing would catch it - the batch tests check values, not layout.

Now four `static_assert`s, per CLAUDE.md's escalation order (a compile-time
contract beats prose). Must-catch verified rather than assumed: adding a
plausible `float glOnly;` to `hudbatch::Vertex` fails the cross-build with
`static assertion failed: input layout stride`. The GL backend gets the same
treatment when it sets its own attribute pointers.

**2. Headless D3D coverage is possible - the header said otherwise and was
wrong.** `hud_gpu_renderer.h` claimed its GPU-only code could not be covered
because it "needs a real device AND a display, and headless CI has neither".
That was inherited, not tested. Measured in this project's own Wine prefix
under Xvfb:

```
d3d11=... d3dcompiler_47=...
CreateDevice(HARDWARE) hr=0x00000000 dev=... featureLevel=0xb000
D3DCompile hr=0x00000000 blob=...
VERDICT: D3D11 PATH VIABLE UNDER WINE
```

Feature level 11_0, `D3DCompile` compiling `vs_4_0`, both S_OK. The header has
been corrected, because a false "this cannot be tested" is worse than no note:
it tells the next reader not to try.

**A harness is therefore buildable and is NOT built yet.** Deliberate call, said
plainly rather than left as an implied TODO: Phase 2 is the main line, the
extraction is already backed by a verbatim diff, 14 unit tests, the new layout
contract and an independent review, and the only remaining gap is whether the
D3D REWIRING still produces correct pixels - which Thomas's two-minute check on
the round-3 list covers. The harness is worth building because it would retire
that gap permanently and stop costing his time as both backends evolve; its
shape is recorded in the corrected header. What it would buy is DIFFERENTIAL
testing (same inputs, same stack, before vs after a refactor), not absolute
correctness - Wine's D3D11 over llvmpipe is not Microsoft's, so a golden image
would pin Wine's output rather than validate Windows'.

---

# Phase 2 built (2026-08-31)

`core/hud_gl_renderer.{h,cpp}`, wired in behind `[Advanced] glInGame` - a
separate key from `overlayInGame` on purpose, so the two can be A/B'd against
each other and against the engine on one machine.

## The decision that shapes the file: it is GL 1.1

Every entry point is a real `opengl32.dll` export resolved with
`GetProcAddress`. Nothing goes through `wglGetProcAddress`, so there are no
VBOs, VAOs, shaders or `glActiveTexture`.

That is what the measurement licenses rather than a preference. Phase 1 showed
the win is BATCHING - one draw call per texture run instead of thousands of
engine primitives - not any modern API feature, and client-side vertex arrays
cleared the bar with room to spare. `wglGetProcAddress` is the genuinely risky
dependency in GL: per-pixel-format, per-driver, and several drivers signal
failure by returning 1, 2, 3 or -1 rather than null. Phase 0 found a 4.6
compatibility context, but that is ONE machine and nothing here assumes it.

Both pixel programs the D3D backend needs shaders for fall out of
fixed-function `GL_MODULATE`: an RGBA texture gives texel x colour (psSprite),
and a `GL_ALPHA` texture leaves RGB as the vertex colour while multiplying only
alpha (psText). Untextured quads go through the batcher's 1x1 white texture, so
one texture stage serves all three cases and no shader path is needed at all.

## Where it draws, and why not in produceFrame

In `PluginManager::handleDraw`, on the Draw callback thread. A GL context is
per-THREAD, and `produceFrame` runs on the worker when `[Advanced] pluginThread`
is on, where there is no context. Suppression follows what ACTUALLY DREW, never
the setting - the same invariant the overlay window pins, and pinned here in
both directions.

## The bug the pixel tests caught immediately

The text case failed on its first run. Cause: a vertical flip on texture upload,
added on the reflex that "GL textures are upside down". They are not. Both APIs
map the FIRST ROW of pixel data to v=0 and merely disagree about what to call
that edge, and the shared batcher derives v from atlas ROW INDICES - so
identical UVs address identical texels either way.

The flip was invisible for sprites, whose UVs span the whole texture (a flip and
a convention swap cancel), and it silently corrupted glyphs, whose UVs are
sub-rectangles. **The quad tests could not have caught it**: they draw through
the 1x1 white texture, which is its own mirror image. Only a test with real
glyph geometry could, which is a useful thing to know about what "we tested the
renderer" is worth when the test data is symmetric.

## Coverage

`tests/integration/tests/gl_render_test.cpp` asserts real rendered pixels -
placement and extent, colour not swizzled, z-order, the text path, the context
coming back byte-identical after a full render, and both directions of the
suppression contract. That is more than the D3D backend has ever had.

It is Mesa/llvmpipe under Wine, so it is weak evidence about any particular
player's GPU and strong evidence about what is the same everywhere. The
conservative API choice above is the answer to the part it cannot cover.

## No state leak was found. To be unambiguous about it.

A status line on this branch read "state leak found in game draw". That wording
was wrong and is corrected here so nobody reconstructs it later: **the GL
backend leaks nothing.** What was built is the state-restore TEST, and it
passes - `gl render: the game's context comes back exactly as it was found`
asserts `stateDiffs == 0` and `glErrors == 0` after a full HUD-shaped render,
using the same 51-value fingerprint Phase 0 measured against a real AMD driver.

It is worth being emphatic because this is the failure mode the whole approach
lives or dies on. Drawing inside the game's context means a leaked bit corrupts
the GAME's next draw, not ours - a garbled game, blamed on the plugin, reported
by users, miserable to debug. So the restore is measured every time rather than
argued for, and the measurement currently says clean.

What is NOT covered: one driver stack (Mesa/llvmpipe under Wine). Round 4's
in-game checklist puts "watch the game, not the HUD" first for exactly this
reason.

## The texture-flip story, because the reasoning behind it will recur

The mistake is more instructive than the fix. "GL textures are upside down" is
a true-sounding thing that nearly every graphics programmer carries, and it is
the wrong lens here: both APIs map the FIRST ROW of pixel data to v=0 and only
disagree about what to CALL that edge. Because the shared batcher derives v from
atlas row indices counted from the first row, identical UVs address identical
texels under either API, and flipping on upload is wrong for both.

Three properties made it dangerous:

1. **It was written down before it was wrong.** `render_batch.h` said a GL
   backend "uploads flipped", so the GL backend flipped. A confident comment
   became a specification for a bug. Both that comment and the plan's copy of
   it are now corrected, and state the reasoning rather than the conclusion.
2. **It was invisible in the obvious test.** Sprites span the whole texture, so
   a flip and a convention swap cancel exactly. Only glyphs - whose UVs are
   sub-rectangles that a flip relocates - could show it.
3. **The quad tests could not have caught it at all**, because they draw through
   the 1x1 white texture, which is its own mirror image. Symmetric test data
   cannot see a symmetry bug. That is the transferable lesson, and it applies
   well beyond textures.

Pinned by the text case in `tests/integration/tests/gl_render_test.cpp`. That
makes it the third thing on this branch that was only safe because a test
existed - after the sweep that measured a draw which never happened, and the
batcher extraction.

## An adversarial re-read of Phase 2 found three bugs of one shape

Worth recording because the shape is the interesting part: the GL 1.1 backend
was using three tokens that are NOT GL 1.1.

| token | actually requires |
|---|---|
| `GL_CLAMP_TO_EDGE` | GL 1.2 |
| `GL_ARRAY_BUFFER_BINDING` | GL 1.5 |
| `GL_VERTEX_ARRAY_BINDING` | GL 3.0 |

On a genuinely old context each raises `GL_INVALID_ENUM`, and `render()`'s
error check turns any GL error into a permanent latch-off. So the backend would
have disabled itself **on exactly the old hardware the GL 1.1 choice exists to
serve** - while working perfectly everywhere the author could test, since Phase
0's machine reports 4.6.

All three are now gated on the version parsed at init (reusing
`glprobe::parseVersion`), and each constant carries the version it needs at its
declaration so the hazard is visible where the token is written rather than only
where it is used. For the two binding queries the gate is not merely safe but
exactly correct: a context predating those bindings cannot have anything bound
to ask about.

The lesson is not "check GL versions". It is that **a compatibility decision
does not enforce itself.** "This backend is GL 1.1" was a real decision, written
in the header in capitals, and three post-1.1 tokens still walked in - because
every one of them worked on the only machine anyone had run it on. The gates and
the per-constant annotations are the enforcement the prose was not.

## A fourth token, and the one failure that raises no GL error

An independent audit of `20d5810` flagged two more post-1.1 tokens the sweep
above had missed. Both are real; one of them turned out to be the more
interesting finding on this branch so far.

**`GL_ARRAY_BUFFER` (GL 1.5) - correct as written, now annotated.** Every use of
it sits behind a non-zero `prevArray`, and `prevArray` can only be non-zero if
the `>= 15`-gated query ran. So the protection is by *data flow* rather than by
a version test. That is sound but invisible: the next reader sees an ungated
post-1.1 token and either "fixes" a non-bug or, worse, hoists a use out from
behind the check. It now says so at its declaration.

**`GL_CURRENT_PROGRAM` (GL 2.0) - declared, gated, and never used.** The dead
constant was the symptom. The constant list was carried over from the Phase 0
probe; the *handling* it belonged to was not.

That handling matters more than the token does, because a bound shader program
is **the one failure mode this backend cannot feel.** The design's safety rests
on a single property: anything that goes wrong raises a GL error, and `render()`
turns any GL error into a clean fallback to engine drawing. A bound program
raises nothing at all. Fixed-function processing is silently replaced, our
vertices go through the *game's* shader, and the HUD comes out garbage or
absent. It degrades to WRONG where every other hazard here degrades to SAFE.

**Phase 0 is not evidence against this, and I had been treating it as though it
were.** `gl_probe.cpp` explicitly unbinds any bound program before drawing its
test quad. Its clean result therefore says nothing whatsoever about whether the
game had one bound - the probe removed the very condition it would have had to
observe. The auditor read this as a lower-likelihood risk; it is not lower, it
is *unmeasured*, and the measurement that looked like it covered it was
structurally incapable of doing so.

The fix is unbind-and-restore, not decline-to-draw. Declining would mean that on
any game frame with a program bound the backend silently never works - the exact
"works everywhere the author can test, dead in the field" failure the version
gates above were written to stop. Unbinding is what the probe already does and
what Phase 0 validated end to end. Resolving `glUseProgram` requires
`wglGetProcAddress`, which this file otherwise refuses; the exception is sound
because it is conditional on a state that implies its own availability - a
program can only be bound on a GL 2.0+ context, where `glUseProgram` necessarily
exists. If it somehow cannot be resolved we do decline, and say so once.

Pinned by `"gl render: a shader program bound by the game does not hijack our
draw"`, which plants a program whose fragment stage paints pure BLUE and then
asks the backend for a RED quad. A regression does not arrive as a subtly wrong
pixel; it arrives blue.

**The transferable lesson is about evidence, not about GL.** A passing probe is
only evidence for the question it left intact. This one normalised away the
condition under test, and the result was read for two phases as covering
something it had actively erased. Worth asking of any green result: what did the
measurement have to change in order to take the measurement?

## Phase 2 shipped with no text and no icons, and every render test passed

An independent reviewer replayed a real 24-rider tape through the backend under
Xvfb and screenshotted the framebuffer. Panel fills, podium colours and the
player-row highlight all drew correctly. Not one glyph. No icons. And the marker
triangles that were visible in a first run with NO assets staged were ABSENT
once real assets were staged - staging assets made it worse.

They flagged it as probably their own staging shortcut. It was not.

### What was wrong

`HudManager` keeps the same table in two shapes:

- `m_fontNames` / `m_spriteNames` - full paths with extensions
  (`mxbmrp3_data\fonts\IBMPlexMono-Regular.fnt`), because that is what the
  game's `DrawInit` wants.
- render names - what `hudsw::Frame` wants, joined by the backend to its own
  asset root. `AssetPath::renderName()` is the converter, and the companion and
  overlay windows both use it.

`renderInContextGl` passed the path tables. The resolver then built
`"plugins/mxbmrp3_data" + "/fonts/" + "mxbmrp3_data\fonts\X.fnt" + ".fnt"`,
which resolves to nothing. `readFile` returns empty, `decodeFnt` reports not-ok,
the resolver returns null, and the batcher's `if (!t) continue;` drops the
primitive. **No GL error. No log line. No exception.** Untextured quads take the
`m_iSprite == 0` white-texture branch and keep drawing perfectly, which is why
the HUD looked like a working renderer right up until you went looking for a
letter. It also explains the vanishing triangles exactly: with no assets found
the HUDs emitted untextured quads, which drew; with assets found they emitted
real sprite indices, which were dropped.

`asset_path.h`'s own header records this bug class happening once before, to
panel themes: "drawQuad silently drew nothing - a whole feature invisible with
no error anywhere." The note was there. I bypassed the function it protects.

### Why the whole render suite passed

`test_gl_render_probe.cpp` builds its own frame, with a hand-written basename:

```cpp
static const std::vector<std::string> kFonts{ "IBMPlexMono-Regular" };
```

So the text case proved the `GL_ALPHA`/`MODULATE` path works - which is true,
and which is exactly what made the failure so confusing to look at. It never
touched the line that was wrong, because the probe supplied by hand the very
value production computed incorrectly. **Verified, not assumed**: reintroducing
the one-line bug and re-running gives 8 of 9 cases still green, with only the
new case red. The pixel tests are provably blind to it.

This is the same lesson as the bound-program finding one section above, arriving
by a different road, and I did not transfer it in the four hours between them: a
test is evidence only for the code it actually routes through. Both times the
instrument quietly substituted a correct value for the one under test.

### The fix, and why it is shaped this way

The derived tables are built in `setupDefaultResources()`, in the same function
that builds the paths, so the two cannot drift. A plain cache is correct here
only because these tables are rebuilt wholesale - that function and `clear()`
are the sole mutators - which is the exact carve-out CLAUDE.md's `PerRider`
invariant names. The comment says so, and says what would break it.

Frame construction moved into one `buildGlFrame()`, so a test can read what the
renderer is actually handed rather than what a probe chose to hand it. That is
the structural half: the new case reads *through the builder*, so re-pointing
the frame at the path tables fails here rather than in someone's screenshot.

Pinned by `"gl render: the frame carries render names, not the game's asset
paths"`. It stages a real `.fnt` through the plugin's OWN user-override sync -
into `savePath\mxbmrp3\fonts\` before `Startup`, which `syncUserAssets` mirrors
into the discovery tree - and `REQUIRE`s that staging worked rather than
degrading to a skip, because a vacuous pass here would be the same failure
shape all over again. It stages an ICON the same way for the sprite half:
sprites are half of what this bug destroyed - the screenshot was missing its
icons exactly as it was missing its glyphs - and a loop over an empty table
asserts nothing. Both halves now `REQUIRE` a non-empty table, so neither can
quietly become decorative.

### What the reviewer got right that I would have missed

They asked the diagnostic question - "are the name tables populated?" - instead
of the debugging question. The tables were populated; the shape was wrong. A
narrower question would have sent me into the glyph path, which is fine.

## Confirmed visually against real data, post-fix

The reviewer re-took the screenshot at `06a7d075`, staging assets the way the
new test does - through the plugin's own user-override sync rather than a hand
copy into `plugins/`. Same tape (`race_farm14_24riders`, 29908 events), same
1280x720 harness window, 6 fonts registered. The HUD renders completely:
standings with race numbers, names, gaps and RET markers; Position and Lap
readouts; the ideal-lap panel with its Session/Alltime times; marker triangles;
the speed widget. Engine handed 0 quads. Text and icons both back.

Worth recording as its own line rather than folding into the test result,
because **the assertion and the picture fail for different reasons.** The
assertion checks the shape of a name; the screenshot checks that a name in the
right shape actually resolves to art on screen through a real driver. Either
could pass while the other fails - a name could be well-formed and point at a
file that is not there, and a picture could be right on llvmpipe while the table
is subtly wrong. That is the argument for keeping both, and it is the same
argument the sweep's `PAINTED` readback made in Phase 1.

## Unexplained: two superimposed strings in the post-fix shot

The post-fix screenshot shows a green italic "ALL-TIME PB" banner and a white
lap time drawn on top of each other, character-for-character, illegible.

**Not the GL backend.** A renderer draws each string where it is told and has no
mechanism to move one onto another; this would look identical through the engine
path. Recorded here, not chased - a spike is not the place to fix HUD layout.

One cheap check was worth doing, and it did not close the question:

- "ALL-TIME PB" is **NoticesHud**, not part of the ideal-lap panel
  (`notices_hud.cpp`), so the "a banner landed on another panel" reading is the
  right shape.
- But their defaults are nowhere near each other. NoticesHud constructs at
  `CENTER_ANCHOR_X` (0.5) and `CenterStack::stackBoxTop()`, which is `rowY(1)` -
  row 1, the top of the screen. IdealLapHud constructs at `cellsX(49)`,
  `cellsY(64)` - row 64. At shipped constructor defaults these **cannot**
  collide.
- NoticesHud also emits exactly ONE string per branch of its priority chain, so
  it is not overlapping itself.

So the overlap is not reproducible from shipped defaults, and the next step is
not more source reading: it is the two strings' actual positions from that run.
Anyone picking this up should dump `m_strings` from the frame that produced the
screenshot and compare the two entries' coordinates. If they match, something
placed them together at runtime; if they differ, the overlap is in how they were
read off the image. Until then this is an observation, not a defect.

## A scoped review of the shipped surface found nine things

Deliberately scoped to what SHIPS - `render_batch`, its D3D rewiring, and the
arbitration in `hud_manager_render.cpp` / `plugin_manager.cpp`. `hud_gl_renderer`
was excluded as spike code that could still be deleted. That scoping is the
right instinct and worth repeating: the extraction has no CI coverage and runs
in the companion window and overlay renderer today, so it carried the real risk.

The extraction came back clean on a mechanical diff against `origin/main`. All
nine findings were in the new wiring, and **all nine verified**. What they have
in common is worth more than the list: none is in the rendering, and none would
have been caught by any test on this branch. They are lifecycle, threading and
arbitration - the seams between the spike and the plugin it is a guest in.

| # | What | Verified by |
|---|---|---|
| 1 | Null deref in the shipping Draw path | `DrawHandler::handleDraw` returns early on null; the block below then dereferenced. The threaded branch had the guard, which is the tell |
| 2 | `mt-plain: game thread only` was FALSE | `processKeyboardInput()` runs inside `produceFrame()`, which is the WORKER thread in pluginThread mode |
| 3 | A failed init was permanent | `hudgl::init()` caches (`if (m_impl) return m_impl->ready`) and the pointer stayed non-null, so the retry gesture could only ever log "render failed" |
| 4 | File I/O + decode inside the game's Draw | By design of the lazy resolver. Recorded, not fixed - see below |
| 5 | `GetProcAddress` every Draw | Just to read `GL_VIEWPORT`, in the path the spike exists to make faster |
| 6 | Leak, plus a header claiming otherwise | No `delete m_glRenderer` anywhere; the header said "owned and deleted in hud_manager_render.cpp" |
| 7 | A test hook that could report stale `true` | `m_glDrewLastFrame` cleared only inside a call both sites skipped on an empty frame |
| 8 | Orphaned doc comment | The GL block was inserted between the sprite-table comment and the methods it describes |
| 9 | `glProbe=2` + `glInGame=1` silently prove nothing | Confirmed below - the important one |

### Finding 2 is the one with a lesson attached

`check_mt_flags.sh` exists to catch exactly this race, and the annotation is what
let it through. A lint can catch a MISSING annotation; nothing can catch a WRONG
one. So the fix is not a corrected comment - it is `std::atomic`, a type that
cannot lie. That reasoning is now recorded at the members themselves.

### Finding 9 had to land before Round 4

The probe's method is a PAIR of bars: one drawn by the engine, one drawn by us
into the GL context, stacked flush so any disagreement between the two
coordinate mappings shows up in one look. With `glInGame=1` the engine frame is
suppressed - and the engine's own cyan reference bar is IN that frame, so it gets
drawn through the GL backend too. The comparison degenerates to GL against GL.
It would still look like agreement. It would prove nothing.

That is the same failure family as the unpainted sweep and the probe that
unbound the program it was meant to observe: **an instrument that quietly
removes the thing it is measuring against, and reports success.** Third time on
this branch. `glProbe >= 2` now wins outright, with `glInGame` refusing to draw
and saying so once.

Pinned by `"gl in-game: the probe wins, so its paired bars stay engine-vs-GL"`,
and must-catch verified: with the guard disabled the case fails on both halves -
the backend draws, and the engine's frame comes back empty, which is exactly the
state in which the probe would have shown two agreeing bars and meant nothing.
Finding 7 is pinned alongside it by a case that turns `glInGame` off and requires
`glDrewLastFrame` to say so on the very next frame.

### Finding 4 is recorded, not fixed, and here is why

The first `glInGame` frame (and every art reload) does `readFile` plus TGA/FNT
decode for every sprite and font, on the game thread. The D3D and software
backends do this on their own window threads. It is real, it is in the path the
spike exists to speed up, and a player would report it as "it stutters when I
turn it on".

Fixing it properly means asynchronous asset loading inside spike code that could
be deleted after Round 4 - which is the kind of work that should follow a go
decision, not precede it. It does not affect the Phase 1 headline, which measured
steady state. It is in the Round 4 sheet as an expected one-time hitch so it is
not reported as a defect, and it is a prerequisite for any ship decision.

## The capability case, which we have not been counting (RECORDING ONLY)

Relayed from the reviewer session, and not covered anywhere above - every
argument in this file so far has been about frame time. There is a second,
independent argument, and it belongs in front of the B5 decision rather than
after it. **Nothing here is built, and none of it touches Round 4.**

The engine's entire drawing vocabulary is two structs. Both verified against
`vendor/piboso/mxb_api.h` rather than taken on trust:

```c
SPluginQuad_t   { float m_aafPos[4][2]; int m_iSprite; unsigned long m_ulColor; }
SPluginString_t { char m_szString[100]; float m_afPos[2]; int m_iFont;
                  float m_fSize; int m_iJustify; unsigned long m_ulColor; }
```

Against `render_batch.h`'s `Vertex { float x, y; float u, v; uint32_t rgba; }`.
The delta is two fields - per-vertex colour and UVs - and both buy things the
native path cannot express at all:

| Missing natively | What per-vertex colour / UVs buy |
|---|---|
| One ABGR per quad | Gradients: panel backgrounds, a map ribbon fading along its length, a gap bar shaded continuously instead of stepping |
| **No UVs whatsoever** - a sprite index takes the whole image | Sub-rectangles: 9-slice borders (panels scale without corner stretch), sprite sheets, animated icons, UV-scrolled bars instead of rebuilt geometry |
| Strings are position + justify, full stop | Quads already take 4 arbitrary corners, so rotation works natively for them; glyphs-as-quads extends that to text - rotated, or following a curve |
| 100-character cap | Gone |
| No clipping, no additive blend | `glScissor`, `glBlendFunc` |

All GL 1.0/1.1, none of it through `wglGetProcAddress`, so none of it spends the
compatibility decision this backend's header defends at length.

**One qualification the note did not carry, and it matters for costing.** The
CP1252 limit is real and is in CLAUDE.md, but "the atlas becomes ours" is not a
switch. `tools/fontgen/mxbmrp3_fontgen.cpp` writes exactly 256 glyph records
indexed by codepoint 0..255 (`Glyph glyphs[256]`, a fixed `268 + 256*40` header),
so UTF-8 in-game needs the FORMAT widened, every shipped `.fnt` regenerated, and
`decodeFnt`/`layoutFnt` re-indexed. Worth having, but it is a project, not a
by-product. Everything else in the table above genuinely does fall out of the
vertex format.

**Why this is a note and not a task.** All three backends consume the same
`hudsw::Frame`, and that frame is shaped like the engine's primitives. Spending
any of this means growing the primitive format, and then the native path has to
degrade. So each row is really a fork: either the HUD looks different per
backend, or it stays at the lowest common denominator and none of it is
reachable.

That makes it a B5 input. If GL becomes primary and native is the compatibility
floor, "gradients flatten and clipping is ignored on native" is a fine trade. If
native must stay a visual peer, none of it can be spent. The decision is
cheaper to make deliberately now than to rediscover after Phase 3.

## Round 4 result: it works on real hardware, and one bug only a game could find

Thomas ran it on an **AMD Radeon RX 6900 XT**, `4.6.0 Compatibility Profile
Context 24.5.1.240502` (parsed 46). Everything headless is Mesa/llvmpipe, so
this is the first evidence from a vendor driver.

**No kill criterion fired.** No graphical corruption in the game itself, no GL
errors, no `hudgl` warnings, and no "render failed". The three warning kinds in
the log are all pre-existing and unrelated (settings-tab measurement,
factory-defaults capture).

**The bound-program question is answered: MX Bikes does NOT leave a shader bound
at Draw time.** Neither log line appears anywhere in the session. The handling
stays as insurance for other games and other drivers, but it is not load-bearing
here - which is worth knowing, because it was assessed as the highest-risk
unmeasured item on the branch.

### Performance, on a real driver

| | `glInGame=1` | `glInGame=0` |
|---|---|---|
| Avg FPS | **546** | 315 |
| Min FPS | 509 | 306 |
| Plugin CPU avg | 0.29 ms | 0.12 ms |

~1.7x frame rate. Note the inversion, which is the whole mechanism in one line:
**our Draw callback got SLOWER** (0.29 vs 0.12 ms) because we now do the drawing
the engine used to do after we returned, **and the frame still got much faster.**
Phase 1 predicted exactly this shape from the sweep; this is it in the game.

Treat 1.7x as an **upper bound** until re-measured: the pack-art bug below meant
GL skipped drawing some textured quads. Both runs submitted the same 1141 quads,
so it should not move far, but the number is not final.

**The quad-count estimate is retired, and it was badly wrong.** The plan inferred
"~260 quads" for a practice profile from screenshot FPS against the sweep floor.
The Benchmark widget reads **1141**. Off by more than 4x. Inferring a load figure
from a frame time and a floor is not a measurement; it was labelled as an
estimate and should have been replaced sooner.

### The bug: nested pack art silently failed to load

Pit board, gamepad and both dials were missing under `glInGame=1`, back at
`glInGame=0`, and present through the overlay renderer. Those are not three
failures - they are the three NESTED PACK asset types.

Render names have two shapes, and `AssetPath::renderName`'s own header says so:
a flat asset is a bare basename resolving under `/textures/` or `/icons/`, while
a nested pack asset (themes, gamepads, pitboards, gauges) keeps a RELATIVE PATH
like `gauges/classic/tacho` and resolves against the asset root directly.
`hudassets::spritePath` is that inverse, and the software renderer calls it.

`hud_gl_renderer` hand-rolled the join and implemented only the flat half. So
icons and flat textures rendered perfectly while every pack's art failed to
load - no GL error, no log line, nothing to grep for. Fixed by deleting the
hand-rolled version and calling the shared helper, so the two cannot diverge
again; `fntPath` likewise.

**This is the third time on this branch that re-deriving something instead of
calling the shared piece has cost a bug** (after the texture flip and the
render-name tables). The pattern is specific enough to name: when the software
renderer already does a thing, the GL backend should CALL it, not reimplement it
from the same understanding. CLAUDE.md's escalation order says the same in
general terms - impossible by construction beats a test - and one implementation
is what makes divergence impossible.

**Why no test caught it.** Every render case drew untextured quads through the
1x1 white texture, or a font. Not one touched sprite path resolution, so five
passing pixel tests said nothing about it. Pinned now by `"gl render: a NESTED
pack sprite resolves and paints"`, which draws the shipped `gauges/classic/tacho`
through the real GL path. Must-catch verified: with the
hand-rolled join restored, that one case fails and the other eleven stay green -
which is the whole story of why this reached a game in the first place.

## Round 4 found a second bug the tests could not: GL context loss

Changing resolution broke every texture AND every glyph. `RELOAD_CONFIG`
appeared to fix it - and that half-fix is what made the shape legible.

A resolution change (and windowed<->fullscreen) **destroys the game's GL context
and creates a new one.** Every texture name the backend cached belonged to the
dead context. The plugin is not reinitialised: the field log shows
`hudgl: GL 1.1 backend ready` exactly once across the whole session, with no
error or warning anywhere. Silent, again.

Why the hotkey seemed to fix it: `requestArtReload` drops the texture cache but
**deliberately keeps fonts** (a `.fnt` is not something anyone iterates on). So
textures returned and glyphs did not. The hotkey was masking half of a problem
it was never the answer to - and had it dropped fonts too, this would have been
recorded as "reload fixes it" and shipped.

**The fix abandons the caches; it does not delete them.** `glDeleteTextures` on
those names in the new context would be actively harmful: they are meaningless
there and may already have been handed to the GAME's own textures, so deleting
them would corrupt the game's rendering - the one failure this whole design
exists to avoid. The dead context took its objects with it; there is nothing
left to free. Detection is `wglGetCurrentContext()` compared against the value
recorded at init - already bound, no `wglGetProcAddress`, no version cost.

Pinned by `"gl render: a context change abandons stale caches and keeps
drawing"`, which tears the harness context down and builds a fresh one.
**Must-catch, with a stated limitation**: with the handling disabled the case
fails on the TEXTURE half but not the glyph half - under Wine/llvmpipe the font
atlas name happened to stay usable across the swap, where in-game both broke.
So one half is proven to catch the regression and the other is not. Recorded at
the assertion rather than left to look like full coverage.

### What Round 4 says about the test suite

Two bugs found in-game, both invisible to thirteen passing tests, and they share
a shape: **the tests exercise a steady state, and both bugs live in a
transition.** Nested pack art needed an asset SHAPE the tests never staged;
context loss needed an EVENT the tests never performed. Neither is exotic - a
player changes resolution, and every pack user has nested art.

The suite is now less blind on both counts, but the general lesson is the one to
carry into Phase 3: a renderer test that only ever renders is testing the easy
half. What breaks a long-lived cache is the thing that happens *to* it.

## The real number, from two exported reports (SUPERSEDED - see the clean pair below)

Baseline vs GL on the same track (`!TimingTestTrack`), one rider, back to back,
~35 s each, synchronous render mode. Read from the exported `BENCH` lines rather
than an on-screen FPS panel, so the percentiles are real.

| metric | baseline | glInGame | delta |
|---|---:|---:|---:|
| FPS avg | 362.8 | 468.1 | **+29.0%** |
| 1% low (p99 fps) | 305.3 | 378.0 | **+23.8%** |
| frame p50 | 2719 us | 2101 us | **-22.7%** |
| frame p99 | 3275 us | 2645 us | **-19.2%** |
| frame max | 6294 us | 5659 us | -10.1% |
| quads drawn | 318 | **442** | +39.0% |
| our Draw callback | 62 us | 171 us | +176% |

**The stutter metrics moved with the average**, which is the part that matters
for a HUD and the part an eyeballed FPS reading cannot show. A change that
improved throughput while worsening p99 would be a bad trade; this is not that.

**Our Draw callback got 2.8x slower, and that is the mechanism, not a cost.**
The engine's draw happens after `Draw()` returns, so taking the work into our
callback moves it somewhere a timer can see. The frame got faster by 618 us
while our callback grew by 109 us; the difference is what the engine was
spending and no longer is.

### Two caveats, and both make the number conservative

1. **The runs are NOT the same HUD set.** `performance_hud` emitted 124 quads
   and 13 strings in the GL run and nothing in the baseline - and 442 - 318 is
   exactly 124. So the GL run drew **39% more geometry** and still won, and it
   also paid ~7 us/frame of extra rebuild CPU for that HUD. Both penalise the
   GL side, so +29% is a floor rather than a headline.
2. **These reports predate the `gl_ingame`/`gl_drew` fields**, so which run was
   which had to be INFERRED - from our Draw callback tripling, which only the GL
   path explains. The inference is sound but it is an inference, and it is
   exactly the ambiguity those fields were added an hour earlier to remove. The
   next pair will say so on their face.

A per-quad engine cost of ~2.3 us/quad falls out of the difference, but only if
non-HUD frame cost was identical across the two runs, which nothing here
establishes. It is not comparable to Phase 1's 0.937 us/quad (different scene,
different method) and should not be quoted as a refinement of it.

### What this settles

This is a LIGHT configuration - 318-442 quads against the 1141 of the loaded
one - which is where the earlier write-up predicted the win would be smallest.
Getting +29% average and +24% on 1% lows there is a stronger result than the
same number on a heavy HUD would have been.

## The clean measurement

The pair above had two flaws; both are gone here. The HUD set is IDENTICAL
across the runs (442 quads, 243 strings in each), and each report states which
renderer it measured on its own face - `gl_ingame=1 gl_drew=1` against
`gl_ingame=0 gl_drew=0`. Same track, one rider, ~34 s each, back to back.

| metric | baseline | glInGame | delta |
|---|---:|---:|---:|
| FPS avg | 343.2 | 467.8 | **+36.3%** |
| 1% low (the stutter metric) | 287.9 | 377.3 | **+31.1%** |
| frame p50 | 2875 us | 2100 us | **-27.0%** |
| frame p99 | 3473 us | 2651 us | **-23.7%** |
| frame max | 8113 us | 5336 us | **-34.2%** |
| frames rendered in ~34 s | 11849 | 15937 | +34.5% |
| our Draw callback | 69 us | 171 us | +148% |

**Every percentile improved.** A change that bought throughput at the cost of
stutter would be a bad trade for a HUD; this is the opposite - p99 fell 24%.

CORRECTED: an earlier version of this paragraph also said "the worst case
improved most" on the strength of frame max falling 34%. The noise-floor
measurement below shows frame MAX varies 22.7% between two runs of the SAME
configuration, so a single-run 34% on that metric is barely outside noise and
should not have been quoted as a finding. p50, p99 and the 1% low all sit far
outside the floor; frame max does not.

**The arithmetic closes.** The frame got 775 us faster at p50 while our own Draw
grew 102 us, so 877 us of engine work disappeared - 1.98 us/quad across the 442
quads. That is the same order as Phase 1's 0.937 us/quad measured synthetically,
on a different scene and by a different method, which is about as much agreement
as those two instruments can be expected to produce.

**The GL side reproduced to within 0.06%** (467.8 vs 468.1 fps) across two
independent runs taken minutes apart, while the baseline moved exactly as its
quad count changed (362.8 fps at 318 quads, 343.2 at 442). A number that is
stable when it should be and moves when it should is the strongest evidence
available here that the instrument is sound - stronger than either run alone.

That closes the performance question this spike opened. The remaining questions
are product ones.

## An accidental noise floor, and what pluginThread adds

The pluginThread pair came back with BOTH reports reading
`threaded=1 gl_ingame=1 gl_drew=1` - the second run's `glInGame=0` never took
effect. **The `gl_ingame` field caught that on its first outing**, an hour after
being added for exactly this reason. Without it the pair would have been
differenced as a comparison and produced a believable, meaningless ~2%.

`glInGame` has no settings-UI control (it is `[Advanced]`, INI-only), so
changing it mid-session means editing the file and pressing RELOAD_CONFIG - and
with Auto-Save on, the plugin's own write can land on top of the edit first.

So the pair is not a comparison. It is two runs of ONE configuration, which is
the more useful accident: it measures the **run-to-run noise floor**, which
nothing until now had.

| metric | run A | run B | spread |
|---|---:|---:|---:|
| FPS avg | 469.5 | 477.2 | **1.6%** |
| 1% low | 375.1 | 389.8 | 3.9% |
| frame p50 | 2091 us | 2060 us | 1.5% |
| frame p99 | 2666 us | 2565 us | 3.8% |
| frame max | 5441 us | 4207 us | **22.7%** |

Two things follow, and the second is a correction to this file:

1. **The GL result is 22x the noise floor** (+36.3% against +/-1.6%). That is not
   a marginal effect and does not need a bigger sample.
2. **Frame MAX is not a usable single-run metric.** It varies 22.7% between
   identical runs, so the "-34.2% worst frame" quoted above is inside its own
   noise. p50, p99 and the 1% low are all far outside theirs; that one is not.
   Corrected in place rather than left standing.

### pluginThread adds ~nothing on top of GL

threaded+GL averages 473.4 fps against sync+GL's 467.8 - **+1.2%, inside the
1.6% noise floor.** They do not stack, and the reason is visible in the report:
with GL on, our whole CPU cost is 42 us/frame against a 2090 us frame, so moving
it to another thread cannot buy much. `pluginThread` exists to keep a hitch on
our side from stalling the game's Draw, and that value is unchanged - it is just
not a throughput win once the engine's draw cost is already gone.

The two features address different risks, which is why the answer is "both, for
different reasons" rather than "one of them".

## The complete 2x2, and an independent replication

The threaded baseline (`threaded=1 gl_ingame=0 gl_drew=0`) closes the matrix.
Same track, same HUD set (442 quads / 240 strings), ~30 s each, one session.

| FPS avg | GL off | GL on | GL effect |
|---|---:|---:|---:|
| **sync** | 343.2 | 467.8 | **+36.3%** |
| **threaded** | 346.5 | 473.4 | **+36.6%** |

**The GL effect replicated to within 0.3 points across two independent render
modes**, and the stutter metrics did the same:

| | 1% low | p50 | p99 |
|---|---:|---:|---:|
| sync | +31.1% | -27.0% | -23.7% |
| threaded | +30.9% | -27.0% | -23.6% |

Those are separate code paths - different `renderInContextGl` call site,
different frame source - measured in separate runs, agreeing to a tenth of a
percent on three metrics. Against a 1.6% noise floor that is about as strong as
this kind of evidence gets on one machine.

**The two features are independent.** GL is worth ~36% whichever thread mode is
set; `pluginThread` is worth ~1% whichever renderer is set (+1.0% with GL off,
+1.2% with GL on) - inside the noise floor both times. They are not alternatives
and they do not compete: `pluginThread` exists so a hitch on our side cannot
stall the game's Draw, which is a latency guarantee, not throughput. Once the
engine's draw cost is gone, our whole CPU is ~44 us against a ~2100 us frame,
and there is nothing left for a thread to hide.

One more datum for the noise-floor argument: this run's frame MAX was 27304 us
(min 36.6 fps) - a single 27 ms spike, against 4207-5441 us in the runs above.
That is the metric already marked unusable, behaving exactly as marked.

### The measurement question is now closed

Four cells, a noise floor, a reproducibility check, and agreement between two
independent code paths. Nothing further is worth measuring on this machine; what
remains is the ecosystem matrix (other GPUs, other drivers, the other games),
which is Phase 3 and needs machines rather than more runs.

## The field found the one that would have broken the alpha: ReShade

A tester ran the pair with ReShade's `opengl32.dll` present and no effects
enabled, and got two identical reports. Not "no measurable gain" - the second
report read `gl_ingame=1 gl_drew=0`. The setting was on and the backend had
never drawn. The log:

```
hudgl: GL 1.1 backend ready - AMD Radeon RX 6900 XT ... (parsed 46)
hudgl: render failed (GL error 0x0500) - falling back to engine rendering
```

Up at 08:36:40.490, dead at 08:36:40.492. Two milliseconds: the first frame.

**GL errors are a QUEUE and `glGetError` pops one at a time.** The end of
`render()` checked for errors to decide whether we had corrupted the context -
but nothing drained the queue on ENTRY. ReShade installs itself as
`opengl32.dll` and does its own GL work; it left a `GL_INVALID_ENUM` behind, our
check read it, concluded we had made it, and latched the backend off for the
session. The error was never ours and we disabled our own feature over it.

The fix drains on entry, so the check at the bottom means what it claims: an
error raised BY OUR CALLS. It logs once when it finds stale errors, because
which layers a player runs is exactly what a field report should carry.

Pinned by `"gl render: somebody else's queued GL error does not latch us off"`,
which plants an error the same way and requires the quad to still draw.
Must-catch verified: with the drain disabled the case fails on `p != -1` - the
backend refusing to render, which is the field symptom exactly.

### Why this one matters beyond the bug

**ReShade is common in sim racing.** Every one of those users would have got
silence: a toggle that appears to do nothing, no error a player would see, and a
plugin that looks like it does not work rather than one that stood down.

And it was only diagnosable because of `gl_drew`, added FOUR HOURS EARLIER for a
different reason - to stop a mislabelled benchmark pair. Without it the report
reads "no performance gain on my machine" and the search goes to the renderer's
speed, which is fine, rather than to whether it ran at all, which is where the
bug was. That is the second time on this branch that an instrument added to keep
a measurement honest turned out to be the thing that identified a defect.

The general form, and the one to carry into Phase 3: **this backend is a guest
in a context other software is also using.** The game is one such user; an
injected layer is another; a future overlay tool is a third. Our checks have to
distinguish "the context is broken" from "we broke it", and until this bug the
code could not tell those apart.

## ReShade confirmed fixed in the field, and a number that raises a question

Same session, ReShade loaded, identical 564-quad HUD, ~30 s each. The report now
reads `gl_ingame=1 gl_drew=1`, and the log carries the new line:

```
hudgl: the GL context already had 1 queued error(s) on entry
       (raised before our Draw, not by us) - drained so our own error check stays honest
```

**Exactly one stale error**, which is ReShade's, now drained instead of blamed on
us. Diagnosis, fix and field confirmation all close on the same single error.

| metric | baseline | glInGame | delta |
|---|---:|---:|---:|
| FPS avg | 419.5 | 494.9 | **+18.0%** |
| 1% low | 351.3 | 415.6 | **+18.3%** |
| frame p50 | 2350 us | 1983 us | -15.6% |
| frame p99 | 2846 us | 2406 us | -15.5% |
| frame max | 3998 us | 3775 us | -5.6% |

### An open question, stated rather than explained away

This is **+18%, where the earlier session measured +36.6%** - and the per-quad
saving fell from 1.74 us/quad to 0.65 us/quad despite MORE quads (564 vs 442),
which should have moved it the other way.

The tempting story is "ReShade adds a fixed per-frame cost, so the same saving is
a smaller share". **That story does not survive the numbers.** The ReShade
BASELINE p50 is 2350 us against the no-ReShade baseline's 2843 us - the game is
*faster* with ReShade loaded, which it cannot be. So something else moved between
the two sessions (a resolution change is the obvious candidate, since one was
made during the context-loss testing and may not have been put back).

Which means: **the two sessions are not comparable, and the +18% cannot be
attributed to ReShade.** What is solidly established is the within-session
result above - GL is worth ~18% on this configuration, with the stutter metrics
moving with it, and the earlier ~36% stands for its own configuration. Anyone
wanting to know what ReShade specifically costs needs a single session with four
runs (ReShade on/off x GL on/off); nothing here answers it.

Recorded as an open question because the alternative is quoting a range whose
ends were measured under conditions that differ in an unknown way - which would
read as a finding and is not one.

## The D3D11 path was verifiable here after all

I deferred the Mode collapse into its own commit saying CI could not see the
D3D11 path, and asked for a Windows eyeball. Both halves of that were weaker
than stated, and this file's own header said so:

1. **The cross-build DOES compile it.** mingw defines `_WIN32`, so
   `hud_gpu_renderer.cpp`'s real Windows path is syntax-checked every build -
   which is exactly how a botched first attempt at the collapse was caught in
   seconds (the `dcompAvailable()` removal ate the top of `init()`).
2. **The RENDERED OUTPUT is checkable too**, and no new harness was needed.
   `tools/hud_window/companion_demo.sh` already opens the real companion window
   headless under Xvfb + Wine and screenshots it - and since the companion is now
   `hud_gpu_renderer`'s only caller, that picture IS this file's output.

Measured across the collapse:

| comparison | pixels differing |
|---|---:|
| before (`804127a`) vs after (`48194e8`) | **0** - byte-identical PNGs |
| software (`hwAccel=0`) vs default | **724053** |

**The second row is the point.** A zero-diff on its own is exactly the shape of
result this branch has been fooled by three times: the unpainted sweep, the probe
that unbound the program it was measuring, and the pixel tests blind to nested
pack art. If both runs had silently fallen back to software, the diff would be
zero for the wrong reason. Forcing `hwAccel=0` and getting 724k differing pixels
proves the instrument can see a difference, which is what turns the zero into
evidence.

The limit is unchanged and worth restating: this is Wine's D3D11 over llvmpipe,
so it pins WINE's output, not Windows'. It is weak for absolute correctness and
exactly right for DIFFERENTIAL work - same inputs, same stack, before versus
after a refactor. A Windows check is still wanted before shipping; what this
removes is the need for one on every future refactor of that file.

The recipe now lives in `hud_gpu_renderer.h`'s header, next to the claim it
retires, rather than in this plan where the next person would not find it.

## The alpha's first field bug: state flows IN, and I had only guarded the way OUT

A tester on `1.29.4.423` (RX 6900 XT, driver `26.8.1.260806`) sent a screenshot
of a HUD in which the themed panels looked correct and every glyph and icon was
a solid block. The log was **completely clean** - backend ready, the stale-error
drain fired once, zero `[ERROR]`, zero `[WARN]`. Toggling `glInGame` 1/0 in game
flipped it garbled/clean every time, so it was ours and it was reproducible.

**The reasoning that found it.** The clean log is the whole clue. This design's
safety rests on "anything that goes wrong raises a GL error, which becomes a
clean fallback" - so a wrong picture with no error means the failure is in the
set of things that raise nothing. That set had already been enumerated once, for
the bound shader program, and the enumeration was treated as complete. It was
not. Listing it properly:

- a bound shader program - handled
- a bound VBO/VAO under client arrays - handled
- **which texture unit our calls address** - not handled
- **every other fixed-function cap that alters a textured, blended triangle** -
  not handled

**The texture unit is the one that predicts the exact symptom split.**
`glTexCoordPointer` feeds the CLIENT-active unit. Nothing here ever selected
one; unit 0 was an assumption about the game, never a statement about our draw.
With the game leaving it on unit 1, our UVs go to a unit that is not drawing and
unit 0 samples ONE CONSTANT TEXEL for the entire frame. That does not read as a
blank HUD - it reads as a HUD whose panels are roughly right and whose text and
icons are solid blocks, because a flat sprite survives being reduced to one of
its own texels and a glyph atlas does not. Separately, a unit ABOVE 0 left
enabled modulates our fragments with its texture and reads its own coord array,
which our vertices do not feed.

The fix normalises both selectors to unit 0 and disables `GL_TEXTURE_2D` on
every higher unit. `glActiveTexture`/`glClientActiveTexture` come through
`wglGetProcAddress`, which this file otherwise refuses; the exception holds on
the same rule as `glUseProgram` - conditional on a state that implies its own
availability, here a context reporting >= 1.3. Init fails loudly if they cannot
be resolved rather than drawing through units it cannot select.

**The rest of the fragment pipeline got the same treatment**, enumerated from
what can alter a textured blended triangle rather than from what has bitten us:
fog, `GL_COLOR_LOGIC_OP`, polygon stipple, texgen S/T/R/Q, clip planes, the
three sample-coverage caps, polygon mode. All of it is inside the two existing
pushes, so it is ours to set and the pops put it back.

### Every plant verified must-catch, and two deliberately left unpinned

`glPlantSilentState(mask)` plants each state independently and
`gl_render_test.cpp` asserts each independently. Verified by removing each
disable from the backend and watching the case fail:

| planted state | failure without the fix |
|---|---|
| unit 1 enabled behind unit 0 | red quad returns **black** |
| client-active unit on 1 | **no text pixel at all** |
| texgen S/T | no text pixel |
| fog | red quad returns **blue** |
| `GL_COLOR_LOGIC_OP` | red quad returns black |
| `glPolygonMode(GL_LINE)` | quad interior unpainted |
| a clip plane | whole frame gone |

`GL_POLYGON_STIPPLE` and the sample caps are disabled by the backend but NOT
pinned, and the test says so: llvmpipe ignores an all-zero stipple (measured -
the quad came back fully red with the disable removed), and the probe context
has no multisample buffer. A case that passes against a broken backend is worse
than no case, and this branch has now been fooled by that shape four times.

**The first plant was one of those.** It set BOTH selectors to unit 1, which is
self-consistent - the backend simply did all its work on unit 1 and came out
correct - so it passed against a deliberately broken backend and I nearly
recorded it as coverage. The hazard is the MISMATCH, which is also what a game
actually leaves: it restores the unit it draws on and forgets the rest.

### A second bug, certain, found by reading rather than by the field

`hudbatch::build` - and therefore every decode and `glTexImage2D` the resolver
triggers - ran BEFORE `glPushAttrib`. So `upload()`'s `GL_UNPACK_ALIGNMENT = 1`
and whatever texture it left bound escaped **permanently into the game's
context**, which is the exact opposite of what `upload()`'s own comment claimed
("restored by the client-attrib pop"). The build now happens inside the save
window, and the comment is true. The `glUseProgram` decline path also returned
with three matrix pushes still on the stack; also fixed.

Note what did not catch either of these: `"the game's context comes back exactly
as it was found"` passes, because its 51-value fingerprint samples neither
`GL_UNPACK_ALIGNMENT` nor `TEXTURE_BINDING_2D`. A restore test is only as good
as its state list, the same way the probe was only as good as the condition it
left intact.

### The diagnostic that should have existed from the start

The backend now logs ONE line on its first frame recording what the game handed
it: active unit, client-active unit, a mask of other enabled units, clip planes,
fog, texgen, logic op, polygon mode. Every value is `0` on a machine that would
have rendered correctly anyway, so the line is signal rather than noise, and a
tester's log now either confirms a diagnosis of this class outright or kills it.

**Status: the fix is a hypothesis that fits every observation, not a confirmed
diagnosis.** It closes a real hole either way - the must-catch table is
independent of what the tester's driver was actually doing - but only his next
run, clean or with that log line populated, settles which state it was.

### The transferable lesson

I had been guarding one direction. "The plugin is a guest in the game's context"
was implemented entirely as *do not leak state OUT*, verified hard, measured
against a real driver. State flowing IN was never guarded at all, and in
fixed-function GL there is a great deal of it that silently changes what a call
MEANS rather than whether it succeeds. The rule the backend now follows:
**fixed-function state we depend on is state we set**, enumerated from the
pipeline, not from the bug list.

## The field bug, and a hypothesis the fix's own confirmation destroyed

A tester on `1.29.4.423` (AMD RX 6900 XT, driver `26.8.1.260806`) reported a
garbled HUD: theme panels correct, **glyphs and icons solid blocks**, from the
first frame, stable, toggling cleanly with `glInGame`. His log had **zero errors
or warnings** - the backend believed it had succeeded.

That combination is diagnostic on its own. The design's safety rests on
everything that goes wrong raising a GL error, which `render()` turns into a
clean fallback. A clean log with a wrong picture means the failure was in the
class that raises nothing - the same class as the bound shader program, which
this file has a section about. So the question was never "what did we get wrong",
it was "what state did we INHERIT and never establish".

The audit found more of it than expected. Every texturing call is PER-UNIT and
"unit 0" was an assumption; `glTexCoordPointer` in particular feeds the
CLIENT-active unit. Beyond the units, the backend disabled the caps it had
thought of and inherited the rest: fog, texgen, `GL_COLOR_LOGIC_OP`, polygon
mode, polygon stipple, clip planes, the sample-coverage caps. Separately,
`hudbatch::build` - and so every decode and `glTexImage2D` - ran BEFORE
`glPushAttrib`, which made `upload()`'s `GL_UNPACK_ALIGNMENT` of 1 and its bound
texture permanent in the game's context, the exact opposite of what `upload()`'s
comment claimed. The rule the file follows now: **fixed-function state we depend
on is state we set**, enumerated from the fragment pipeline rather than from the
ones that have bitten us.

Six planted states are pinned in `gl_render_test.cpp`, each verified must-catch
by removing its disable and watching the case fail. Polygon stipple and the
sample caps are deliberately NOT pinned and say so: llvmpipe ignores a planted
stipple and the probe context has no multisample buffer, so those cases would
pass against a broken backend.

**The tester's confirmation, and what it cost.** He reports it renders correctly
and gains ~15 fps (150 → 165). It also refuted the mechanism I had published to
explain it. I had reasoned that his newer driver would report a smaller
`GL_MAX_TEXTURE_UNITS`, making unit 7 not a fixed-function unit there, and told
Thomas to look for `(of N)` differing as the confirmation. The two lines are
byte-identical:

```
activeUnit=7 clientActiveUnit=7 otherUnitsEnabled=0x00 (of 8) clipPlanes=8
fog=1 texGenS=0 logicOp=0 polyStipple=0 polyMode=0x1b02
```

Both machines. Same `(of 8)`. So the incoming state is not the variable at all -
**both drivers are handed the identical context, and only one renders it
correctly.** 24.5.1 draws fixed-function on unit 7; 26.8.1 does not. The fix
works because unit 0 is the path every driver actually exercises.

Which state was the culprit on his machine - unit 7, fog, or the combination -
is NOT known, and isolating it would mean a volunteer running builds with one
state re-enabled at a time for an answer that changes no code. Recorded as
unknown rather than closed. **The lesson worth keeping is that a fix confirming
in the field is not evidence for the story attached to it.** The diagnostic log
line is what settled this, and it only settled it because it prints what was
FOUND rather than what was concluded - had it logged "unit count below the
fixed-function threshold: yes/no", it would have agreed with me.

## Text, icons, and a map fix that measured itself shut

Three quality findings after the state fix, in descending order of how much
they mattered.

**Text was undersampled, and it was never a GL bug.** The shipped `.fnt` cell is
135px so scaled-up widgets stay crisp; ordinary HUD text is ~20px. Every normal
string therefore MINIFIES the atlas about 7x, and one bilinear tap reads 4 texels
of a ~49-texel footprint - dropping precisely the partial-coverage texels along a
stroke's edge, so strokes thin and break up rather than simply blurring. The game
mipmaps that atlas; fontgen widened its inter-glyph padding from 4px to 20px
specifically because the games "render the whole atlas down its mip chain". The
packing needed to do this safely had been shipped for a long time and we were the
only renderer not using it. Measured through the real renderer at 20px:
394 lit pixels / mean coverage 201 without the chain, 805 / 93 with it. Icons
followed, with a premultiply so a transparent surround's black bytes do not ring
each icon with a halo.

**The lesson is about the instrument, and I got it wrong twice before getting it
right.** My before/after screenshots showed ZERO change, and I spent several turns
re-reading correct code looking for a bug that was not there.

First wrong turn: `companion_demo.sh` defaults to `hwAccel=1`, so I had been
photographing the D3D11 backend while changing the software one. Fair enough.

Second wrong turn, and the one worth recording: I then "forced `hwAccel=0`",
got a byte-identical capture, and concluded the demo's seeded settings were not
reaching the plugin at all - that TESTING.md's claim of a pixel-diffable software
renderer was false. I wrote that in a commit message (`926e952`) and in this file.
IT WAS NOT TRUE. `hwAccel` lives in `[Advanced]`, and I had put it under
`[Display]`. The key never existed; the tool did exactly what it says. Measured
once the section was right: D3D11 versus software differs by **751,596 px**, and
mips on versus off within the software path by **71,897 px** - the change I had
convinced myself was invisible.

So the transferable rule is narrower and more useful than "the tool lies": when an
instrument returns a null result, suspect its INPUT before its integrity - and be
slowest of all to escalate "my measurement disagreed with me" into "this
documented gate is broken", because that claim outlives the session in a way a
wrong diagnosis does not. A unit test on the real renderer did settle it faster
here, but not because the tool could not; because I was holding it wrong.

**The map ribbon closed itself off, cheaply.** Its edges are `SOLID_COLOR` quads
at arbitrary angles - no texture, so mipmaps are irrelevant. The one cap the state
audit had missed was `GL_MULTISAMPLE`, left inherited because I had convinced
myself AA was irrelevant to a HUD of rectangles (true of panels and text, false of
exactly this). Enabling it changed nothing visible, which is ambiguous between
"did nothing useful" and "nothing to do" - so the one-shot log gained
`GL_SAMPLE_BUFFERS`/`GL_SAMPLES` rather than an argument. Answer:
`sampleBuffers=0 samples=0`. There is no multisample buffer, so the enable is a
guaranteed no-op there, and since the engine draws into the same framebuffer the
engine path has no AA on those quads either.

Then the decisive one, because the first reading was ambiguous: MX Bikes' own
antialiasing was set to **16x**. Re-measured in FULLSCREEN, with it still at 16x:
`sampleBuffers=0` again. So this is not a windowed-pixel-format quirk and not a
setting that needed a restart - the game RESOLVES its multisample target before
the Draw callback, and hands us a framebuffer with no samples in it. Hardware
antialiasing is therefore structurally unavailable to the HUD in this game, on
EITHER path: the engine draws into the same buffer we do.

That leaves only a renderer-independent fix, and its cost is why it is recorded
rather than built: a texture ramp feathers PROPORTIONALLY (10px on a wide ribbon,
1px on a thin one), so a constant ~1px edge needs geometry - a full-alpha core
plus two fading strips, 3x the ribbon quads. Free under GL at ~0.007 us/quad;
~375 us on a 200-quad ribbon through the engine at ~0.94 us/quad, about 18% of the
frame budget. GL-only would dodge that at the price of the two paths drawing the
same map differently.

**Both diagnostics that settled something printed what they FOUND rather than
what they concluded** - the texture-unit line, and this one. A field log phrased
as "multisampling unavailable: yes/no" would have been an answer to my question;
`sampleBuffers=0 samples=0` is an answer to the situation.
