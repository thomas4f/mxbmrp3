# Web overlay tests (Playwright)

Headless Playwright tests for the OBS/browser overlay in `../../mxbmrp3_data/web`,
mostly driving its built-in **`?demo` mode** and asserting the rendered DOM. What
this layer is for, and why `overlay_snapshot.spec.js` uses a real plugin snapshot
instead of the demo, is in [`../../TESTING.md`](../../TESTING.md) → *Layer 4*.

## Run

```bash
./run.sh                  # or: npm test
./run.sh --headed         # watch the browser drive the overlay
./run.sh -g "race phase"  # filter by test title
```

## Lint

`./lint.sh` (`--fix` to apply) runs ESLint over every `.js` in the repo from the
same npm install; `eslint.config.mjs` documents the rules it turns off and why.

`run.sh` installs deps on first use and resolves Chromium (a no-op when a
preinstalled browser is already on `PLAYWRIGHT_BROWSERS_PATH`). Requires Node.js;
the overlay is served by a throwaway `python3 -m http.server` (see
`playwright.config.js`).

## Screenshot the overlay (visual review, no game)

To *eyeball* the rendered overlay - iterating on a panel, theming, or a new chart -
capture a headless PNG of `?demo`. This is the **browser overlay** path; the
in-game/companion HUD is a different renderer with its own harness
(`tools/hud_window/`, Wine + Xvfb) - don't confuse the two.

```bash
cd tests/web && npm ci    # first use only (deps + Chromium)
python3 -m http.server 8199 --directory ../../mxbmrp3_data/web >/dev/null 2>&1 &
node - <<'JS'
import('@playwright/test').then(async ({ chromium }) => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1920, height: 1080 } });
  p.on('pageerror', e => console.error('PAGEERROR', e));   // catch overlay JS errors
  // ?speed=8 fast-forwards past the warmup; networkidle + a settle wait lets panels animate in.
  await p.goto('http://127.0.0.1:8199/index.html?demo&speed=8', { waitUntil: 'networkidle' });
  await p.waitForTimeout(2500);
  await p.screenshot({ path: '/tmp/overlay-demo.png' });
  await b.close();
});
JS
```

Serve from `mxbmrp3_data/web/` directly so `overlay-*.js`/`style.css`/`custom.css` resolve
as they do in production. Add `&speed=` to reach the timed race quickly; drop it to
see the warmup. For real-data preview instead of the synthetic demo, use
`tools/replay --web` (real tape → live plugin → browser).

## What's covered

Each spec's header says what it pins:

- `overlay.spec.js` - the tower (a full, ordered field read by on-screen Y, real
  roster names, session + clock, no uncaught JS errors), the race-phase `Leader`
  gap, and the bottom-slot panels/battle lifecycle.
- `overlay_snapshot.spec.js` - the committed real-plugin snapshot through `render()`.
- `assets.spec.js` - every `index.html`/`style.css`/`sw.js` reference resolves and
  the overlay scripts load in the same order everywhere.
- `custom_css.spec.js` - the `custom.css` override contract.
- `empty_panels.spec.js` - forced-empty placeholder behaviour.
- `tail.spec.js` - the down-the-order scroller.
- `mobile.spec.js` - the phone fill-width mode (device-emulated `pointer: coarse`).
- `parity.spec.js` - the JS side of `../fixtures/cpp_js_parity.json`, plus the
  `overlayPanelName()` ↔ `createSlotPanel` name lockstep.
- `status_lines.spec.js` - the overlay's own connection lines in the event log.
- `trnfix_web.spec.js` - `tools/trnfix/web/index.html`, the trainer triage page.
- `usage_page.spec.js` - the usage survey's Pages copy written by
  `tools/analytics_page.py`.

## Notes / gotchas

- **Don't put these under `mxbmrp3_data/web/`** - that folder is synced to the
  user's Documents on game start, so test files there would ship. Hence the
  separate top-level `tests/web/`.
- Rows use a **stable DOM order + `translateY`** for smooth position-change
  animation, so DOM order ≠ visual order; assertions sort by on-screen Y and
  find the leader by its position value. Tests freeze CSS transitions first so a
  mid-animation read is deterministic.
- Riders below the **Max Riders** cutoff stay in the DOM as `display:none`;
  ranking assertions filter to the shown rows.
- CI installs the Chromium revision `@playwright/test` pins; `run.sh` locally uses
  whatever Chromium it finds first, so local green is not proof of CI green
  (TESTING.md → *Layer 4*).
