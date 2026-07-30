// ============================================================================
// tests/web/tests/empty_panels.spec.js
// Every bottom-slot panel a broadcaster can force with a hotkey must, when forced
// before its data exists, render the SAME "No data" placeholder. This guards the
// fix for the old three-way inconsistency: the lap boards showed "No data" in the
// normal font, session-charts showed "No lap data yet" in a different (display)
// font, and down-the-order / best-sectors refused to appear at all.
// ============================================================================
const { test, expect } = require('@playwright/test');
const os = require('os');
const path = require('path');

const SHOTS = process.env.MXB_SHOTS || path.join(os.tmpdir(), 'mxb-overlay-shots');

// name -> { id, titleSel, title }. All five are forceable via the OVERLAY_FORCE_* hotkeys.
// The placeholder shows the panel's own title above the shared "No data" row.
const PANELS = {
  fastlap: { id: 'fastlap-panel', titleSel: '.fastlap-title', title: 'Fastest Last Lap Times' },
  bestlap: { id: 'bestlap-panel', titleSel: '.bestlap-title', title: 'Fastest Laps' },
  charts: { id: 'charts-panel', titleSel: '.charts-title', title: 'Session Charts' },
  sectors: { id: 'sectors-panel', titleSel: '.sectors-title', title: 'Best Sectors' },
  tail: { id: 'tail-panel', titleSel: '.tail-title', title: 'Down the Order' },
};

test('every forced-empty broadcast panel shows the same "No data" placeholder', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));

  // maxRiders:0 (show all) => no riders hidden below the cutoff, so down-the-order is
  // forceable-empty. A race demo has no sector data ever, and laps start empty — so at
  // the flag-drop every lap/sector/charts panel is genuinely empty when forced.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({ maxRiders: 0 }));
  });
  await page.setViewportSize({ width: 900, height: 1100 });

  const seen = [];
  for (const [name, p] of Object.entries(PANELS)) {
    // Fresh load per panel so it is forced at the flag-drop (no laps yet) — the first
    // completed lap would give the lap/charts panels real data and defeat the test.
    // speed=0.1 (the demo's floor) and a huge slot dwell together hold the
    // assertion's window open. Both are load-bearing, and neither is about
    // making the test "slower to be safe":
    //
    //   - At the default speed=1 the demo completes a lap ~5s in, which gives the
    //     lap/charts panels REAL data and replaces the very placeholder row this
    //     test reads. 0.1 pushes that past a minute. (Instrumented: the row
    //     existed only between t=1.0s and t=5.0s.)
    //   - A forced panel still auto-hides when its dwell expires (~6.5s here,
    //     CONFIG.slotDuration scaled by the demo). Once it slides out the row is
    //     gone for good.
    //
    // Either expiry ends the window, after which getComputedStyle has no element
    // to read and the polls below can NEVER converge — so under load this failed
    // ~40% of the time, and RAISING the timeouts made it worse, not better, by
    // guaranteeing the read landed after the window rather than inside it. The
    // fix is to stop the window closing, not to wait longer for a closed one.
    await page.goto('/index.html?demo&race&speed=0.1');
    await expect(page.locator('#standings-body .standings-row').first()).toBeVisible({ timeout: 30_000 });
    await page.evaluate(() => { window.CONFIG.slotDuration = 600_000; window.CONFIG.slotRest = 0; });
    await page.evaluate((n) => window.mxbmrp3ForceSlot(n), name);

    const panel = page.locator('#' + p.id);
    const emptyRow = panel.locator('.board-row.board-empty');
    // The forced panel slides in and shows exactly one shared "No data" row...
    await expect(emptyRow, `${name} shows one placeholder row`).toHaveCount(1, { timeout: 10_000 });
    // ...under its own correct title.
    await expect(panel.locator(p.titleSel), `${name} title`).toHaveText(p.title);
    const readInfo = () => emptyRow.locator('.board-name').evaluate((el) => ({
      text: el.textContent.trim(),
      font: getComputedStyle(el).fontFamily,
    }));
    // The font is asserted identical across panels below. A one-shot read has
    // flaked (transient computed style straight after the slide-in), so every
    // read is polled: the FIRST panel until two consecutive reads agree (a
    // stable reference — without this the reference itself could be the
    // transient), and each later panel until it matches that reference. Bounded
    // waits that converge when the transient settles but still fail on a
    // genuine, persistent font difference.
    // `last` records the exact read that satisfied the poll — pushing a FRESH
    // readInfo() afterwards was a race: the poll could pass on a settled value
    // and the re-read then catch the next transient (the under-load flake where
    // the final same-font check saw 2 fonts despite every per-panel poll passing).
    let last = null;
    if (seen.length) {
      await expect
        .poll(async () => { last = await readInfo(); return last.font; },
              { message: `${name} font settles to the shared font` })
        .toBe(seen[0].font);
    } else {
      // Reference panel: require a NON-EMPTY font stable across two consecutive
      // reads. Chromium returns "" from getComputedStyle while the panel is
      // mid-slide/masked (not rendered) — the observed flake was exactly this
      // test capturing "" as the reference and then failing every later panel
      // against it. Empty is "not ready yet", never a valid reference.
      let prev = null;
      await expect
        .poll(async () => {
          last = await readInfo();
          const stable = last.font !== '' && last.font === prev;
          prev = last.font;
          return stable;
        }, { message: `${name} font stabilizes non-empty (reference panel)` })
        .toBe(true);
    }
    seen.push({ name, ...last });
    await page.screenshot({ path: path.join(SHOTS, 'empty-' + name + '.png') });
  }

  // Consistency: identical wording and identical font family across every panel.
  expect(new Set(seen.map((s) => s.text)), 'same wording everywhere').toEqual(new Set(['No data']));
  expect(new Set(seen.map((s) => s.font)).size, 'same font everywhere').toBe(1);
  // And it is NOT the display/marker font the old session-charts note used.
  expect(seen[0].font.toLowerCase()).not.toContain('sansman');

  expect(errors).toEqual([]);
});

// Regression: down-the-order is the one panel whose title is set DYNAMICALLY by its
// normal path (buildTailList -> "Positions N–M"). A forced-empty show after a real one
// must reset the title, or the "No data" row appears under a stale "Positions …" header.
test('down-the-order forced-empty resets its title (no stale "Positions" header)', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.setViewportSize({ width: 900, height: 1100 });
  // Default maxRiders (20) with the 22-rider field => riders hidden below the cutoff, so
  // a REAL down-the-order show sets the dynamic "Positions …" title.
  await page.goto('/index.html?demo&race&speed=2');
  await expect(page.locator('#standings-body .standings-row').first()).toBeVisible({ timeout: 30_000 });

  const tail = page.locator('#tail-panel');
  const title = tail.locator('.tail-title');

  // 1) Real show: dynamic "Positions …" title, no placeholder row.
  await page.evaluate(() => window.mxbmrp3ForceSlot('tail'));
  await expect(tail).toHaveClass(/tail-in/, { timeout: 10_000 });
  await expect(tail.locator('.board-row.board-empty')).toHaveCount(0);
  await expect(title).toHaveText(/Positions/, { timeout: 10_000 });

  // 2) Reveal the whole field (no hidden riders) so a re-force is empty, then hand the
  //    slot to another panel and back so down-the-order rebuilds through renderEmpty.
  //    CONFIG is a top-level global (the classic-script scope), readable/writable here.
  await page.evaluate(() => { window.CONFIG.maxRiders = 0; });
  await page.evaluate(() => window.mxbmrp3ForceSlot('fastlap'));
  await expect(page.locator('#fastlap-panel')).toHaveClass(/fastlap-in/, { timeout: 10_000 });
  await page.evaluate(() => window.mxbmrp3ForceSlot('tail'));
  await expect(tail.locator('.board-row.board-empty')).toHaveCount(1, { timeout: 10_000 });

  // The title is reset to its default — NOT the stale "Positions …" over "No data".
  await expect(title).toHaveText('Down the Order');
  expect(errors).toEqual([]);
});
