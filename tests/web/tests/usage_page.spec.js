// ============================================================================
// tests/web/tests/usage_page.spec.js
// The usage survey's Pages copy (index.html beside REPORT.md, written by
// tools/analytics_page.py): charts inlined, and a readout following the cursor.
//
// WHY A BROWSER TEST. The readout is the one part of the report that nothing
// else can see: GitHub shows the charts as <img>, where the script never runs,
// and the Python selftest only proves the data attributes are written. This
// drives the real script on the page the generator's --demo writes (synthetic
// data through the real chart helpers), so a rename on either side -- a
// data-chart field, a data-tip, the tooltip's markup -- fails here.
// ============================================================================
const { test, expect } = require('@playwright/test');
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const ROOT = path.resolve(__dirname, '../../..');
let pageUrl;

// The generator's one pip dependency. CI installs it (tests.yml, web-overlay
// job), so this skip can only happen on a developer box - where it names the fix.
function hasMarkdownIt() {
  try {
    execFileSync('python3', ['-c', 'import markdown_it'], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}
test.skip(!hasMarkdownIt(), 'markdown-it-py not installed (./tools/install_deps.sh analytics)');

test.beforeAll(() => {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'usage_page_'));
  execFileSync('python3', [path.join(ROOT, 'tools/analytics_page.py'), '--demo', out]);
  pageUrl = 'file://' + path.join(out, 'index.html');
});

test('a line chart reads out every series on the day under the cursor', async ({ page }) => {
  await page.goto(pageUrl);
  const chart = page.locator('figure.chart svg[data-chart]').first();
  const tip = page.locator('.tip');
  await expect(tip).toBeHidden();

  // The demo's 10 days span the plot evenly; day index 5 is "09-06", where the
  // three lines stand at 31 / 33 / 36.
  const d = JSON.parse(await chart.getAttribute('data-chart'));
  const box = await chart.boundingBox();
  const scale = box.width / Number(await chart.getAttribute('width'));
  await page.mouse.move(box.x + d.x[5] * scale, box.y + (d.top + 40) * scale);

  await expect(tip).toBeVisible();
  await expect(tip.locator('.head')).toHaveText('09-06');
  // Highest first, each with its own value; 1.28.0 is at 0% by now, so it is
  // left out rather than listed as a "0%" row.
  await expect(tip.locator('.row .name')).toHaveText(['Other', '1.29.5', '1.30.3']);
  await expect(tip.locator('.row .val')).toHaveText(['36%', '33%', '31%']);
  await expect(chart.locator('.cursor')).toBeVisible();

  // Day 0: the 1.30.3 line has not started, so it is left out, not shown as 0.
  await page.mouse.move(box.x + d.x[0] * scale, box.y + (d.top + 40) * scale);
  await expect(tip.locator('.head')).toHaveText('09-01');
  await expect(tip.locator('.row .name')).toHaveText(['1.29.5', 'Other', '1.28.0']);
  await expect(tip.locator('.row .val')).toHaveText(['60%', '35%', '5%']);

  // Off the plot, it goes away.
  await page.mouse.move(box.x + 2, box.y + 2);
  await expect(tip).toBeHidden();
});

test('a bar, and a tier segment, read out their own value', async ({ page }) => {
  await page.goto(pageUrl);
  const tip = page.locator('.tip');
  await page.locator('rect[data-tip="Windows 10: 21% (2,779)"]').hover();
  await expect(tip).toHaveText('Windows 10: 21% (2,779)');
  // A segment reads its share and count, the same shape as the labels; a sliver
  // under 1% reads "<1%", not "0%"; an achievement tier adds its requirement as
  // a second line.
  await page.locator('rect[data-tip^="Racer: Gold <1%"]').hover({ force: true });
  await expect(tip).toHaveText('Racer: Gold <1% (5)\nFinish 100 races');
  expect(await tip.evaluate(el => el.innerText)).toBe('Racer: Gold <1% (5)\nFinish 100 races');
  // A one-shot has no tiers, so its segment names no "Bronze".
  await page.locator('rect[data-tip^="Metronome"]').hover({ force: true });
  expect(await tip.evaluate(el => el.innerText)).toBe('Metronome: 3% (30)\nFive laps in a row, all within a tenth');
  // The achievement's name reads out what it asks, tier by tier.
  await page.locator('text[data-tip^="Racer"]').hover();
  expect(await tip.evaluate(el => el.innerText))
    .toBe('Racer\nBronze: Finish a race\nSilver: Finish 10 races\nGold: Finish 100 races');
});

test('the charts are inlined, with ids that cannot collide', async ({ page }) => {
  await page.goto(pageUrl);
  await expect(page.locator('figure.chart svg')).toHaveCount(3);
  await expect(page.locator('img[src^="charts/"]')).toHaveCount(0);
  // No <title> inside an inline chart: the browser would show it as a second,
  // native tooltip over every mark, on top of the page's own readout.
  await expect(page.locator('figure.chart svg title')).toHaveCount(0);
  await expect(page.locator('figure.chart svg[aria-label="Version migration"]')).toHaveCount(1);
  const ids = await page.locator('[id]').evaluateAll(els => els.map(e => e.id));
  expect(new Set(ids).size).toBe(ids.length);
  // Every clip-path still points at a clip that exists.
  const dangling = await page.locator('[clip-path]').evaluateAll(els =>
    els.filter(e => !document.getElementById(e.getAttribute('clip-path').slice(5, -1))).length);
  expect(dangling).toBe(0);
});

test('the section links wrap instead of scrolling sideways', async ({ page }) => {
  await page.setViewportSize({ width: 400, height: 800 });
  await page.goto(pageUrl);
  const nav = page.locator('.top nav');
  // The demo has two sections; the real report has eleven. Add enough links
  // that one line cannot hold them, which is the case that scrolled.
  await nav.evaluate(el => {
    for (let i = 0; i < 10; i++) {
      const a = document.createElement('a');
      a.href = '#x';
      a.textContent = 'Feature & HUD adoption ' + i;
      el.appendChild(a);
    }
  });
  const [scrollW, clientW] = await nav.evaluate(el => [el.scrollWidth, el.clientWidth]);
  expect(scrollW).toBeLessThanOrEqual(clientW);
  const pageW = await page.evaluate(() => document.documentElement.scrollWidth);
  expect(pageW).toBeLessThanOrEqual(400);
});

test('the header scrolls away with the page instead of framing it', async ({ page }) => {
  // A pinned header read as a frame with the report scrolling inside it.
  await page.setViewportSize({ width: 1400, height: 300 });
  await page.goto(pageUrl);
  await page.evaluate(() => window.scrollTo(0, 400));
  const bottom = await page.locator('.top').evaluate(el => el.getBoundingClientRect().bottom);
  expect(bottom).toBeLessThanOrEqual(0);
});

test('the headline figures render as cards, not the Markdown table', async ({ page }) => {
  await page.goto(pageUrl);
  const cards = page.locator('.glance .card');
  await expect(cards.locator('.name')).toHaveText(['Unique installs', 'Releases since Nov 2025']);
  await expect(cards.locator('.num')).toHaveText(['14,782', '39']);
  await expect(page.locator('th', { hasText: 'At a glance' })).toHaveCount(0);
});
