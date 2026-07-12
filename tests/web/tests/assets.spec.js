// ============================================================================
// tests/web/tests/assets.spec.js
// Guards the overlay's asset wiring — the regression class introduced when the
// former flat web root was split into js/ fonts/ icons/ and the monolithic
// app.js into ordered js/overlay-*.js scripts. A moved/renamed file with a
// stale reference (index.html, style.css, or sw.js's PRECACHE_URLS) would ship
// a 404 that only surfaces in a browser/OBS. These tests fail the build first.
// ============================================================================
const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const WEB_DIR = path.resolve(__dirname, '../../../mxbmrp3_data/web');
const read = (p) => fs.readFileSync(path.join(WEB_DIR, p), 'utf8');
const toPath = (u) => '/' + (u === './' ? '' : u.replace(/^\.?\//, ''));

// The quoted entries of the PRECACHE_URLS array in sw.js.
function precacheUrls() {
  const m = read('sw.js').match(/PRECACHE_URLS\s*=\s*\[([\s\S]*?)\]/);
  expect(m, 'PRECACHE_URLS array present in sw.js').toBeTruthy();
  return [...m[1].matchAll(/["']([^"']+)["']/g)].map((x) => x[1]);
}

// <script src> / <link href> referenced by index.html.
function indexRefs() {
  const html = read('index.html');
  return {
    scripts: [...html.matchAll(/<script\s+src="([^"]+)"/g)].map((x) => x[1]),
    links: [...html.matchAll(/<link[^>]+href="([^"]+)"/g)].map((x) => x[1]),
  };
}

// url(...) targets in style.css (skip data:/absolute).
function cssUrls() {
  return [...read('style.css').matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)]
    .map((x) => x[1])
    .filter((u) => !/^(data:|https?:|\/\/)/.test(u));
}

test('every asset referenced by index.html resolves (no dangling paths after the folder reorg)', async ({ request }) => {
  const { scripts, links } = indexRefs();
  expect(scripts.length).toBeGreaterThanOrEqual(11);   // the split overlay scripts
  for (const src of scripts) {
    expect((await request.get(toPath(src))).ok(), `<script src="${src}">`).toBeTruthy();
  }
  for (const href of links) {
    if (href === 'custom.css') continue;   // user-optional; 404s harmlessly by design
    expect((await request.get(toPath(href))).ok(), `<link href="${href}">`).toBeTruthy();
  }
});

test('every url() in style.css resolves (fonts/ and icons/ moved into subfolders)', async ({ request }) => {
  const urls = cssUrls();
  expect(urls.length).toBeGreaterThan(0);
  for (const u of urls) {
    expect((await request.get(toPath(u))).ok(), `style.css url(${u})`).toBeTruthy();
    // Assets moved into subfolders — the reference must carry the folder prefix.
    expect(/\.(ttf|svg)$/.test(u) ? /^(fonts|icons)\//.test(u) : true, `url(${u}) is folder-qualified`).toBe(true);
  }
});

test('every PRECACHE_URLS entry in sw.js resolves, and custom.css is never precached', async ({ request }) => {
  const urls = precacheUrls();
  for (const u of urls) {
    expect((await request.get(toPath(u))).ok(), `sw.js precache "${u}"`).toBeTruthy();
  }
  // custom.css is a user override served no-cache and excluded from the SW cache —
  // precaching it would pin a stale (or absent) file. Must never appear here.
  expect(urls.some((u) => u.endsWith('custom.css'))).toBe(false);
});

test('the split overlay scripts are all wired into index.html and sw.js, in the same load order', async () => {
  const onDisk = fs.readdirSync(path.join(WEB_DIR, 'js')).filter((f) => f.endsWith('.js')).sort();
  const htmlJs = indexRefs().scripts.filter((s) => s.startsWith('js/')).map((s) => s.slice(3));
  const swJs = precacheUrls().filter((u) => u.startsWith('js/')).map((u) => u.slice(3));

  // No overlay-*.js on disk is left unreferenced (a new split file can't be forgotten).
  expect([...htmlJs].sort()).toEqual(onDisk);
  // index.html and sw.js list the SAME scripts in the SAME order. The scripts share
  // one global scope with no module boundary, so load order is load-bearing.
  expect(swJs).toEqual(htmlJs);
});
