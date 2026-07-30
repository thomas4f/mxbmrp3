// ============================================================================
// tests/web/tests/parity.spec.js
// JS side of the cross-boundary mirror parity tests.
//
// Helpers mirrored between the plugin and the overlay carry "keep them in
// step" comments; these tests make that mechanical:
//
//  * isColorDark (overlay-util.js ↔ PluginUtils::isColorDark) and
//    fmtChartSecs (overlay-charts.js ↔ session_charts_math.h formatSecs) are
//    evaluated in the REAL overlay page (they're global classic-script
//    functions) against the shared golden vectors in
//    tests/fixtures/cpp_js_parity.json — the SAME file
//    tests/unit/test_cpp_js_parity.cpp asserts against the C++
//    implementations, so the two renderers can only pass together.
//
//  * The broadcaster panel-force names: HttpServer::overlayPanelName() (C++)
//    emits the name the client matches against createSlotPanel names
//    (overlay-panels.js). A renamed/removed panel on either side makes the
//    force hotkey silently do nothing, so the sets are compared here by
//    parsing both sources.
// ============================================================================
const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '../../..');
const FIXTURE = JSON.parse(
  fs.readFileSync(path.join(ROOT, 'tests/fixtures/cpp_js_parity.json'), 'utf8'));

test('isColorDark matches the shared C++/JS golden vectors', async ({ page }) => {
  await page.goto('/?demo');
  for (const c of FIXTURE.isColorDark) {
    const got = await page.evaluate((hex) => isColorDark(hex), c.hex);
    expect(got, `isColorDark("${c.hex}")`).toBe(c.dark);
  }
});

test('fmtChartSecs matches the shared C++/JS golden vectors', async ({ page }) => {
  await page.goto('/?demo');
  for (const c of FIXTURE.formatSecs) {
    const got = await page.evaluate(
      ([ms, sign]) => fmtChartSecs(ms, sign), [c.ms, c.showSign]);
    expect(got, `fmtChartSecs(${c.ms}, ${c.showSign})`).toBe(c.out);
  }
});

test('sector-resolution chart helpers match the shared golden vectors', async ({ page }) => {
  await page.goto('/?demo');
  for (const c of FIXTURE.cumulativeBySector) {
    const got = await page.evaluate(
      ([s, n]) => cmCumulativeBySector(s, n), [c.sectors, c.sectorsPerLap]);
    expect(got, c._why || 'cmCumulativeBySector').toEqual(c.out);
  }
  for (const c of FIXTURE.lapsAtSectorIndex) {
    const got = await page.evaluate(
      ([i, n]) => cmLapsAtSectorIndex(i, n), [c.index, c.sectorsPerLap]);
    expect(got, `cmLapsAtSectorIndex(${c.index}, ${c.sectorsPerLap})`).toBeCloseTo(c.out, 6);
  }
  for (const c of FIXTURE.latestPositionExtent) {
    const got = await page.evaluate((pos) => cmLatestPositionExtent(pos), c.positions);
    expect(got, c._why || 'cmLatestPositionExtent').toEqual({ top: c.top, bottom: c.bottom });
  }
  for (const c of FIXTURE.traceValueAtSector) {
    const got = await page.evaluate(
      ([r, i, n, cum]) => cmTraceValueAtSector(r, i, n, cum),
      [c.refPaceMs, c.index, c.sectorsPerLap, c.cumulativeMs]);
    expect(got, c._why || 'cmTraceValueAtSector').toBe(c.out);
  }
  for (const c of FIXTURE.xFracForLaps) {
    const got = await page.evaluate(
      ([l, f, m]) => cmXFracForLaps(l, f, m), [c.laps, c.firstLaps, c.maxLaps]);
    expect(got, c._why || 'cmXFracForLaps').toBeCloseTo(c.out, 6);
  }
});

test('every C++ overlayPanelName maps to a registered createSlotPanel name', async () => {
  // C++ side: the string literals returned inside HttpServer::overlayPanelName().
  const cpp = fs.readFileSync(path.join(ROOT, 'mxbmrp3/core/http_server.cpp'), 'utf8');
  const fnMatch = cpp.match(
    /overlayPanelName\(int panel\)\s*\{([\s\S]*?)\n\}/);
  expect(fnMatch, 'overlayPanelName() found in http_server.cpp').toBeTruthy();
  const cppNames = [...fnMatch[1].matchAll(/return\s+"([^"]+)"/g)].map((m) => m[1]);
  expect(cppNames.length, 'C++ side lists forceable panel names').toBeGreaterThanOrEqual(5);

  // JS side: the `name:` values registered through createSlotPanel() — panels
  // live in overlay-panels.js AND overlay-charts.js (the charts carousel), so
  // scan every overlay script, anchored to the createSlotPanel call.
  const jsDir = path.join(ROOT, 'mxbmrp3_data/web/js');
  const jsNames = [];
  for (const f of fs.readdirSync(jsDir).filter((f) => f.endsWith('.js'))) {
    const src = fs.readFileSync(path.join(jsDir, f), 'utf8');
    for (const m of src.matchAll(/createSlotPanel\(\s*\{[\s\S]{0,200}?name:\s*"([^"]+)"/g)) {
      jsNames.push(m[1]);
    }
  }
  expect(jsNames.length, 'createSlotPanel registrations found').toBeGreaterThanOrEqual(6);

  for (const name of cppNames) {
    expect(jsNames, `C++ panel name "${name}" registered via createSlotPanel`)
      .toContain(name);
  }
});
