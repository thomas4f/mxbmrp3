// ============================================================================
// tests/web/tests/custom_css.spec.js
// Exercises the user "lightweight theme override" workflow: copy custom-sample.css
// to custom.css, edit it, and the overlay picks it up. index.html links custom.css
// (absent by default → 404s harmlessly), and it must load AFTER style.css so its
// rules win the cascade. Colours/fonts are synced from the in-game palette as
// inline styles, so an override of those needs !important (documented in CLAUDE.md);
// this test uses !important, as a real user would.
//
// The override is COLOUR-ONLY (no geometry): while custom.css briefly exists on the
// shared served dir, parallel tests in other workers that load it assert positions/
// text/sizes — never these colours — so they can't be thrown off. Removed in finally.
// Service workers are blocked so each fresh context fetches custom.css from the
// network deterministically (the SW's asset-cache path is covered by assets.spec.js).
// ============================================================================
const { test, expect } = require('@playwright/test');
const fs = require('fs');
const os = require('os');
const path = require('path');

const WEB_DIR = path.resolve(__dirname, '../../../mxbmrp3_data/web');
const CUSTOM = path.join(WEB_DIR, 'custom.css');
const SHOTS = process.env.MXB_SHOTS || path.join(os.tmpdir(), 'mxb-overlay-shots');

const PINK = 'rgb(255, 0, 170)';
const NAVY = 'rgb(0, 60, 120)';

async function render(browser) {
  const ctx = await browser.newContext({ viewport: { width: 900, height: 1040 }, serviceWorkers: 'block' });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto('/index.html?demo&speed=30', { waitUntil: 'domcontentloaded' });
  await expect(page.locator('#standings-body .standings-row').first()).toBeVisible({ timeout: 30_000 });
  await page.addStyleTag({ content: '*{transition:none!important;animation:none!important}' });
  return { ctx, page, errors };
}

test('custom.css: a user override re-themes the overlay and loads after style.css', async ({ browser }) => {
  fs.mkdirSync(SHOTS, { recursive: true });
  try {
    // 1) Baseline — no custom.css on disk (the <link> 404s harmlessly, no JS error).
    if (fs.existsSync(CUSTOM)) fs.rmSync(CUSTOM);
    {
      const { ctx, page, errors } = await render(browser);
      const base = await page.evaluate(() => ({
        title: getComputedStyle(document.getElementById('header-title')).color,
        header: getComputedStyle(document.getElementById('standings-header')).backgroundColor,
      }));
      expect(base.title).not.toBe(PINK);   // default theme, not our override
      expect(base.header).not.toBe(NAVY);
      await page.screenshot({ path: path.join(SHOTS, 'custom-css-before.png') });
      expect(errors).toEqual([]);
      await ctx.close();
    }

    // 2) The user creates custom.css and edits it (with !important, as documented).
    fs.writeFileSync(CUSTOM,
      `/* user override */\n` +
      `#header-title      { color: ${PINK} !important; }\n` +
      `#standings-header  { background: ${NAVY} !important; }\n`);
    {
      const { ctx, page, errors } = await render(browser);   // fresh context → fresh fetch
      const now = await page.evaluate(() => ({
        title: getComputedStyle(document.getElementById('header-title')).color,
        header: getComputedStyle(document.getElementById('standings-header')).backgroundColor,
        // custom.css must come AFTER style.css so its rules win the cascade.
        order: (() => {
          const hrefs = [...document.querySelectorAll('link[rel=stylesheet]')].map((l) => l.getAttribute('href'));
          return { style: hrefs.indexOf('style.css'), custom: hrefs.indexOf('custom.css') };
        })(),
      }));
      expect(now.title).toBe(PINK);
      expect(now.header).toBe(NAVY);
      expect(now.order.style).toBeGreaterThanOrEqual(0);
      expect(now.order.custom).toBeGreaterThan(now.order.style);
      await page.screenshot({ path: path.join(SHOTS, 'custom-css-after.png') });
      expect(errors).toEqual([]);
      await ctx.close();
    }
  } finally {
    if (fs.existsSync(CUSTOM)) fs.rmSync(CUSTOM);
  }
});
