# Web overlay tests (Playwright)

Headless browser tests for the OBS/browser overlay in `../mxbmrp3_data/web`. They
drive the overlay's built-in **`?demo` mode** — a synthetic 22-rider warmup +
race that feeds the same snapshots into `render()` that the live SSE stream
would — and assert the **rendered DOM**. No game, no plugin, no network.

This is the client-side complement to the C++ integration tests: those assert the
plugin's `/api/state` JSON (what the overlay *receives*); these assert what the
overlay *draws* from it (tower rows, ordering, gaps, session/clock).

## Run

```bash
./run.sh                  # or: npm test
./run.sh --headed         # watch the browser drive the overlay
./run.sh -g "race phase"  # filter by test title
```

`run.sh` installs deps on first use and resolves Chromium (a no-op when a
preinstalled browser is already on `PLAYWRIGHT_BROWSERS_PATH`). Requires Node.js;
the overlay is served by a throwaway `python3 -m http.server` (see
`playwright.config.js`).

## Screenshot the overlay (visual review, no game)

To *eyeball* the rendered overlay — iterating on a panel, theming, or a new chart —
capture a headless PNG of `?demo`. This is the **browser overlay** path; the
in-game/companion HUD is a different renderer with its own harness
(`tools/mxbmrp3_hud_window/`, Wine + Xvfb) — don't confuse the two.

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
`tools/mxbmrp3_replay --web` (real tape → live plugin → browser).

## What's covered

`tests/overlay.spec.js`:
- **Tower renders a full, ordered field** — the 22-rider demo fills the tower;
  the visible rows carry contiguous positions `1..N`, ascend top-to-bottom on
  screen (rows are `translateY`-slotted over a stable DOM order, so ranking is
  read by on-screen Y, not DOM index), every row has a name, real roster names
  come through, the session type + clock render, and **no uncaught JS errors**.
- **Race phase + leader gap** — `?speed=` fast-forwards the demo past the warmup
  into the timed race; the P1 row's gap column shows the `Leader` label (race
  gap semantics), not a lap time.

## Notes / gotchas

- **Don't put these under `mxbmrp3_data/web/`** — that folder is synced to the
  user's Documents on game start, so test files there would ship. Hence the
  separate top-level `tests/web/`.
- Rows use a **stable DOM order + `translateY`** for smooth position-change
  animation, so DOM order ≠ visual order; assertions sort by on-screen Y and
  find the leader by its position value. Tests freeze CSS transitions first so a
  mid-animation read is deterministic.
- Riders below the **Max Riders** cutoff stay in the DOM as `display:none`;
  ranking assertions filter to the shown rows.
- The pinned `@playwright/test` version matches the Chromium the tests download,
  so the browser and API can't drift.
