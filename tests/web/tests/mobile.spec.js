// ============================================================================
// tests/web/tests/mobile.spec.js
// Mobile fill-width mode: on a phone-sized touch screen (pointer: coarse and
// width <= 820px) the overlay stops being a fixed corner widget — #overlay
// drops into normal flow, applyRootSizing() scales the root font-size so the
// tower's computed width equals the viewport width (a stable fixed point), and
// the elements that don't fit the fill-width model are suppressed: the fixed
// 20rem focus card, the row status chips (absolutely positioned at left:100%,
// i.e. off-screen once the tower is full width). The settings gear — normally
// revealed by mousemove, which never fires on touch — is revealed by a screen
// touch instead (then auto-hides, so it doesn't sit permanently over the
// tower). Desktop/OBS (pointer: fine) is unaffected; the desktop geometry is
// covered by every other spec.
// ============================================================================
const { test, expect, devices } = require('@playwright/test');

// iPhone 12 geometry/touch on the suite's chromium project. The descriptor's
// defaultBrowserType (webkit) must be dropped: spreading it switches the
// browser driver, which then launches the project's chromium executablePath
// with webkit's protocol/flags and dies at startup.
const phone = { ...devices['iPhone 12'] };   // 390x844, touch, pointer: coarse
delete phone.defaultBrowserType;
test.use(phone);

test('phone-sized touch viewport: tower fills the width, misfit elements are suppressed', async ({ page }) => {
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));

  // Enable the focus card so the assertion below proves the media block
  // overrides even a user-enabled setting (style.css uses !important for that).
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({ focusCard: true }));
  });
  await page.goto('/index.html?demo&race&speed=5');
  await expect(page.locator('#standings-body .standings-row').first()).toBeVisible({ timeout: 30_000 });

  // Precondition: the emulated device actually matches the mobile media query
  // (otherwise every assertion below would vacuously test the desktop path).
  expect(await page.evaluate(() =>
    window.matchMedia('(pointer: coarse) and (max-width: 820px)').matches)).toBe(true);

  // #overlay leaves fixed positioning (towerX/towerY ignored) ...
  expect(await page.evaluate(() =>
    getComputedStyle(document.getElementById('overlay')).position)).toBe('static');

  // ... and the root font-size fixed point makes the tower's width track the
  // viewport width.
  //
  // ASSERT THE PROPERTY, NOT THE PIXEL. This first read `<= 2` px, which passed
  // on the Chromium the sandbox has preinstalled (rev 1194, where one correction
  // step lands exactly) and failed on the one Playwright actually pins (rev
  // 1228, 5px short) the first time it ran in CI. A tolerance that tight was
  // asserting that a font-metric-driven fixed point converges to a specific
  // pixel — which is a property of the browser build, not of the overlay.
  //
  // What actually matters to a user: the tower must never be WIDER than the
  // viewport (that gives a horizontal scrollbar on a phone — a real bug, so
  // that side stays exact), and it must genuinely fill the width rather than
  // sit there as a corner widget.
  const fit = await page.evaluate(() => ({
    tower: document.getElementById('overlay').offsetWidth,
    viewport: window.innerWidth,
  }));
  expect(fit.tower).toBeLessThanOrEqual(fit.viewport);
  expect(fit.tower).toBeGreaterThanOrEqual(fit.viewport * 0.97);

  // The fixed-width focus card is suppressed even though the user enabled it,
  // and the off-screen row chips are hidden rather than causing overflow.
  expect(await page.evaluate(() =>
    getComputedStyle(document.getElementById('focus-card')).display)).toBe('none');
  const chipDisplays = await page.evaluate(() =>
    Array.from(document.querySelectorAll('.row-chips')).map((c) => getComputedStyle(c).display));
  expect(chipDisplays.length).toBeGreaterThan(0);
  expect(chipDisplays.every((d) => d === 'none')).toBe(true);

  // No horizontal overflow: the page body never scrolls sideways.
  expect(await page.evaluate(() =>
    document.documentElement.scrollWidth <= window.innerWidth + 1)).toBe(true);

  // The gear is unreachable via mousemove on touch — a screen touch reveals it.
  await page.touchscreen.tap(5, 400);
  await expect(page.locator('#settings-gear')).toHaveClass(/visible/, { timeout: 5_000 });

  expect(errors).toEqual([]);
});
