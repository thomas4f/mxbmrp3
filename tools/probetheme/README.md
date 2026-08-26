# Pinpointing what the engine actually charges us for

Enabling a theme cost **+1606 µs of frame time** and **+12 µs of plugin CPU** in a
matched pair of benchmark runs (v1.29.0.588, settings panel open in both). So ~99%
of a theme's cost is the engine drawing our primitives, after `Draw()` returns,
where no in-plugin timer can see it. What we hand over went from 1274 quads to 2331.

That comparison moves **four** variables at once, and we do not yet know which one
we are paying for:

| variable | theme off | theme on |
|---|---|---|
| quad **count** | 1 flat background quad per panel | 27 slice quads per panel |
| **textured** vs flat | `m_iSprite = 0` (fill with `m_ulColor`) | a real sprite |
| texture **switches** | none | up to 27 distinct sprites per panel |
| texture **size** | n/a | whatever the art is |

This directory plus the in-plugin render probe separates all four.

The per-panel arithmetic, for reference: **+26 quads flat** on 21 of the 29 themed
panels - that is `frame` (9) + title `band` (9) + content `card` (9), minus the one
flat quad it replaces. Panels with extra sections or buttons pay another 9 each;
`settings_hud` alone is **+172**.

## How to run it: one button

Settings → Performance → **Run sweep** (the button toggles; press again to stop
early). Deliberately a button rather than a hotkey - a binding that floors your
frame rate for a minute is a key you press by accident. It steps the whole matrix itself - nineteen
configurations, ~2.8 s each, about a minute total - and writes one
`probe_sweep_<timestamp>.txt` into the benchmarks folder with the differences
already computed.

There is no INI editing and no per-step benchmark cycle. That is deliberate: the
hand-driven version failed on its first outing and failed *silently*, producing five
internally-perfect reports of an experiment that never happened, because nothing errors
when a probe fails to engage. Hand-stepping also spreads the runs over minutes of
wall-clock, during which the machine can change state underneath the comparison, and
invites the natural mistake of changing N *during* a benchmark window - which averages
the Ns together instead of comparing them.

### What to have switched on: nothing

**Turn every HUD off, select no theme, and leave the benchmark widget off too.** The
sweep does its own frame timing and writes its own report; it needs no other panel.

This is not just tidiness. The sweep is differential, so a HUD load would cancel in
principle - but in practice a loaded frame hurts three ways: less headroom, so the
probe's effect is a smaller fraction of the total; per-frame HUD rebuild cost that
varies with content (lap times ticking, standings updating), which adds between-step
variance that has nothing to do with the probe; and a baseline already near some
other ceiling, where adding 2000 quads measures the ceiling rather than the quads.

An empty baseline also gives a free correctness check: with no HUDs, the report's
`Quads` column should equal `N` exactly on every step.

### Reading the report

The step table is **frame time**, never FPS - the cost of N primitives is additive in
time and hyperbolic in rate, so a rate would have to be converted back before any of
it could be differenced. `d p50` is the rise over the baseline step and `us/prim` is
that rise divided by N, which is the number the whole exercise exists to produce.

The `=== DERIVED ===` block does the subtractions for you:

- **quad, untextured, tiny** - what a quad costs simply to exist.
- **+ texturing** - textured-pinned minus untextured.
- **+ switching** - textured-cycled minus textured-pinned. *This is the row that
  decides whether the fix is "draw fewer boxes" or "draw them from fewer sprites",
  and those imply completely different work.*
- **fill** and the **textured fill premium** - cost per screen of coverage. A
  nine-slice's centre is a large stretched textured quad, so this is the case a
  themed panel actually is.
- **text string** - the other primitive array, of which we hand over ~930 a frame.

Keep the run stationary at a fixed spot: the sweep controls its own variables, but
not the scene behind them.

## The fourth variable: texture size

The probe cannot answer this one. Pinning two *existing* sprites of different
resolution compares two pictures that also differ in alpha coverage and detail, so
the difference is not attributable to size. `probetheme.py` generates themes that
are the same design at different resolutions:

```
probetheme.py --out <themes-dir> --size 16
probetheme.py --out <themes-dir> --size 1024
```

| run | theme | probe | what it isolates |
|---|---|---|---|
| R7 | `_probe_16` | off | baseline: 27 slices, 27 sprites, tiny textures |
| R8 | `_probe_1024` | off | identical quads, sprites and geometry; 4096× the texels |

**R8 − R7 is the resolution cost, and nothing else.** The slices are seeded from the
stem name only, so the same slice is the same design at every size, and alpha is
fully opaque so both cover identical pixels.

Two constraints that are load-bearing rather than tidiness:

- **Install one probe theme at a time.** `HudManager` registers *every discovered*
  theme's sprites at `DrawInit`, not just the selected one, so an unselected 1024px
  theme may still be resident while you measure. Delete the folder between runs.
- The art is **noise, and deliberately ugly**. A flat colour samples one texel
  everywhere and stays in the texture cache at any resolution - a flat probe would
  measure nothing and confidently report "resolution is free".

## What it found (v1.29.0.590, 19/19 steps, empty baseline 2476 us)

Three N per family, and every family's three points agree on one slope - so the
per-primitive model is measured now, not assumed. The slopes below are derived
**between** N points, which makes them independent of the baseline step entirely.

| primitive | µs each | |
|---|---|---|
| quad, untextured, tiny | **1.00** | what a quad costs simply to exist |
| quad, textured, one sprite | 1.05 | **+0.05 - texturing is ~free (+5%)** |
| quad, textured, cycling sprites | 1.67 | **+0.62 - switching is real (+62%)** |
| quad, FULL SCREEN | ~0 above baseline | **fill rate is not a cost at all** |
| text string, 15 chars | **2.66** | 2.7× a quad - **but see below: this is per 15 glyphs** |

**It is the primitive COUNT, not what the primitive is.** Ten full-screen quads -
ten entire screens of overdraw - cost *less* than ten tiny ones. The cost is paid per
primitive submitted and essentially nothing per pixel covered.

**Texture size needs no separate test after all.** A full-screen textured quad is
~2M texel fetches and costs nothing measurable over an untextured one; if two million
fetches are free, resolution cannot matter. The R7/R8 probe-theme runs below are
therefore **not worth running** - `probetheme.py` stays for the day something makes
that question live again.

One honest caveat: all six full-screen deltas came out slightly *negative* (−25 to
−51 µs, about 1–2% under baseline), which is a systematic offset rather than noise -
most likely the GPU clocking up during the heavy cycling steps that ran just before.
It does not change the reading, which is one-directional: adding ten screens of fill
cannot be a cost if it does not raise frame time.

### The model predicts what we already measured

A theme adds 1057 quads and cost +1606 µs. At the bare-quad rate that is 1053 µs; at
the cycling-textured rate, 1764 µs. Measured sits between - most themed quads cause a
switch, some do not. Fully accounted for.

The whole HUD, likewise: a full frame is 6892 µs against a 2476 µs empty baseline, so
4416 µs for 2331 quads + 936 strings. The model says 2449 + 2491 = 4940 µs, ~12%
over, which is the batching the engine does manage.

### Text, and the mistake the first sweep made

**The text figure above is per FIFTEEN-CHARACTER string, and that matters.** The
engine bills per glyph. Reported as a flat "2.66 µs per string" and applied to the
plugin's own strings - which average about nine characters - it overstated the cost
of drop shadow by **1.7×**. The sweep now steps string LENGTH as well as count, and
the report gives a slope (µs per glyph) and an intercept (µs per string before its
first glyph); price a real string as `intercept + slope × length`.

The check that caught it is the one worth keeping: convert an observed FPS change to
frame time and see whether the model predicts it.

| toggle, from 145.2 fps | Δ frame time | model says | |
|---|---|---|---|
| theme off (+50 fps) | −1759 µs | 1057 quads × 1.669 = **1764 µs** | exact |
| drop shadow off (+20 fps) | −826 µs | 547 strings × 2.661 = 1456 µs | **1.7× too high** |

Working backwards, 826 µs over 547 shadow strings is 1.51 µs each ≈ 8.5 characters at
the probe's per-glyph rate - which is exactly what a HUD string looks like. The model
was right; the instrument was measuring a longer string than the plugin draws.

**Never compare FPS deltas directly.** +20 fps and +50 fps from the same baseline are
not in the ratio 20:50 - they are 826 µs and 1759 µs, a ratio of 1:2.1. Frame time is
additive; frame rate is not.

### Ranked, then

| lever | worth | note |
|---|---|---|
| **quad count** | ~1.67 µs each themed | the frame+band+card nesting is 27 quads per themed panel; a theme is 1.76 ms |
| **drop shadow** | ~0.83 ms | one extra string per string (`collectSurface`); already a user setting, and nobody knew it cost this |
| **sprite switching** | up to ~0.65 ms themed | included in the 1.67 above. Slices within one nine-slice do not overlap, so grouping a panel's quads by sprite is z-order-safe |
| fill / overdraw | nothing | measured at zero |
| texture resolution | nothing | implied zero by the full-screen textured rows |

