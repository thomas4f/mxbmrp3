// ============================================================================
// tests/web/tests/tail.spec.js
// Down-the-order (the "tail" panel) WITH data — the coverage panel that shows
// the riders hidden below the Max Riders cutoff. empty_panels.spec.js covers
// its forced-EMPTY path; this pins the real one: forced with a populated tail,
// it lists exactly the below-cutoff riders (contiguous positions, cutoff+1..N)
// under a "Positions A–B" title, sizes its viewport to min(slotRows, cap,
// tailLen) rows, and actually runs its scroll pass (down and back — the panel
// is a scroller, not a static board). Down-the-order has NO auto-trigger by
// design (it's coverage, not a story) — force is the only path, which is what
// this exercises via the demo force hook.
// ============================================================================
const { test, expect } = require('@playwright/test');

test('down-the-order lists the below-cutoff riders and scrolls through them', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));

  // Hide riders below P8 (22-rider demo => 14 tail riders), keep the slot free
  // (no competing auto-show panels), 5 visible rows, a short Panel Time so the
  // five-phase scroll pass runs inside the test window.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({
      maxRiders: 8, slotRows: 5, slotDuration: 5,
      fastLap: false, bestLap: false, sectors: false, battle: false, charts: false,
    }));
  });
  await page.goto('/index.html?demo&race&speed=5');
  await expect(page.locator('#standings-body .standings-row').first()).toBeVisible({ timeout: 30_000 });
  await page.waitForFunction(() => typeof window.mxbmrp3ForceSlot === 'function', { timeout: 30_000 });
  await page.evaluate(() => window.mxbmrp3ForceSlot('tail'));

  const panel = page.locator('#tail-panel');
  const rows = panel.locator('.tail-row');

  // The full hidden range is in the track (every below-cutoff rider), and the
  // title names it. 22 riders, cutoff 8 => positions 9..22.
  await expect(rows).toHaveCount(14, { timeout: 10_000 });
  await expect(panel.locator('.tail-title')).toHaveText(/Positions 9[–-]22/);

  // Rows are the below-cutoff riders in order: contiguous positions 9..22.
  const cleaned = await panel.locator('.tail-row .col-pos').evaluateAll(
    (els) => els.map((e) => parseInt(e.textContent, 10)).filter((n) => !isNaN(n)));
  expect(cleaned.length).toBe(14);
  expect(cleaned[0]).toBe(9);
  expect(cleaned[cleaned.length - 1]).toBe(22);
  for (let i = 1; i < cleaned.length; i++) expect(cleaned[i]).toBe(cleaned[i - 1] + 1);

  // Viewport sized to min(slotRows, cap, tailLen) rows = 5 here (a whole number
  // of measured rows — no sliver row).
  const sized = await page.evaluate(() => {
    const vp = document.querySelector('#tail-panel .tail-viewport');
    const row = document.querySelector('#tail-panel .tail-row');
    return { vp: vp.getBoundingClientRect().height, row: row.getBoundingClientRect().height };
  });
  expect(Math.round(sized.vp / sized.row)).toBe(5);
  expect(Math.abs(sized.vp - 5 * sized.row)).toBeLessThan(1.5);

  // The scroll pass runs: the track translates down (negative Y) ...
  const trackY = () => page.evaluate(() => {
    const tr = getComputedStyle(document.querySelector('#tail-panel .tail-track')).transform;
    if (!tr || tr === 'none') return 0;
    return new DOMMatrixReadOnly(tr).m42;
  });
  await expect.poll(trackY, { timeout: 10_000 }).toBeLessThan(-1);
  // ... and comes back up to the top before the panel ends its turn.
  await expect.poll(trackY, { timeout: 10_000 }).toBe(0);

  expect(errors).toEqual([]);
});
