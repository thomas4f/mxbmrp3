// ============================================================================
// tests/web/tests/overlay_snapshot.spec.js
// The JS half of the overlay data contract: the real client renders a REAL
// plugin snapshot.
//
// Every other spec here drives `?demo`, whose snapshot is hand-written in
// overlay-demo.js — so the overlay is only ever tested against a shape the
// overlay itself made up. If buildJsonSnapshot() renames a field, the demo keeps
// emitting the old one and the whole suite stays green while the live overlay
// renders nothing.
//
// tests/fixtures/overlay_snapshot.json is captured from the actual plugin by
// tests/integration/tests/overlay_snapshot_test.cpp (which fails when the
// plugin's shape drifts from it). This spec feeds that same file to the same
// render() the SSE stream calls — overlay-connection.js does
// `render(JSON.parse(event.data))`, so this is the production path with real
// bytes, not a mock. A field the overlay reads that the plugin no longer sends
// fails HERE.
// ============================================================================
const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '../../..');
const SNAPSHOT = JSON.parse(fs.readFileSync(
  path.join(ROOT, 'tests/fixtures/overlay_snapshot.json'), 'utf8'));

// Plain "/" rather than "?demo": the demo loop would immediately overwrite the
// fixture with its own synthetic snapshot, which is the very thing this spec
// exists to not test against.
async function renderFixture(page) {
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto('/');
  await page.addStyleTag({ content: '*{transition:none !important;animation:none !important}' });
  await page.waitForFunction(() => typeof window.render === 'function');
  await page.evaluate((snap) => render(snap), SNAPSHOT);
  return errors;
}

// Same read as overlay.spec.js: rows are translateY-slotted over a stable DOM
// order, and rows below the Max-Riders cutoff stay in the DOM as display:none.
function readShownRows(page) {
  return page.evaluate(() =>
    Array.from(document.querySelectorAll('#standings-body .standings-row'))
      .filter((e) => getComputedStyle(e).display !== 'none')
      .map((e) => ({
        pos: (e.querySelector('.col-pos')?.textContent || '').trim(),
        name: (e.querySelector('.col-name')?.textContent || '').trim(),
        gap: (e.querySelector('.col-gap')?.textContent || '').trim(),
        y: e.getBoundingClientRect().top,
      })));
}

test('the real client renders a real plugin snapshot', async ({ page }) => {
  const errors = await renderFixture(page);

  const want = SNAPSHOT.standings;
  expect(want.length, 'fixture has no field to render').toBeGreaterThan(1);

  const rows = await readShownRows(page);
  expect(rows.length, 'tower row count does not match the snapshot field').toBe(want.length);

  // Names reached the DOM, row by row. This is the assertion that breaks on a
  // renamed snapshot field: the row still renders, just empty.
  //
  // Against fullName, not name: the snapshot carries BOTH, and the tower reads
  // `rider.fullName || rider.name` — `name` is the plugin's pre-truncated
  // in-game-width fallback ("Ali"), and the overlay deliberately prefers the
  // full one so the user can widen the column. Asserted as a prefix because
  // the client then truncates to CONFIG.nameChars, which is user-configurable.
  // NOT `fullName || name`, deliberately, even though that is what the client
  // does: mirroring the client's fallback here makes the test launder the exact
  // drift it exists to catch. Renaming fullName away in the snapshot silently
  // degrades every tower row to the 3-char abbreviation, and a test that falls
  // back in step with the client sees two identical strings and passes. So the
  // preferred field is required outright.
  rows.forEach((row, i) => {
    const expected = want[i].fullName;
    expect(typeof expected,
      `standings[${i}] has no fullName — the tower prefers it over the pre-truncated ` +
      `name, so losing it degrades every row to the in-game abbreviation`).toBe('string');
    expect(row.name.length, `row ${i + 1} (#${want[i].num}) rendered an empty name`)
      .toBeGreaterThan(0);
    expect(expected, `row ${i + 1} shows "${row.name}", not a prefix of "${expected}"`)
      .toContain(row.name);
  });

  // Positions are the contiguous 1..N the snapshot describes, in screen order.
  const ys = rows.map((r) => r.y);
  expect(ys, 'rows are not in ascending screen order').toEqual([...ys].sort((a, b) => a - b));
  expect(rows.map((r) => r.pos)).toEqual(want.map((r) => String(r.pos)));

  expect(errors, 'uncaught JS while rendering a real snapshot').toEqual([]);
});

test('session and gap fields from a real snapshot reach the header and rows', async ({ page }) => {
  await renderFixture(page);

  // The header reads from snapshot.session — a rename there empties it while the
  // tower assertions above would still pass.
  const header = await page.evaluate(() => ({
    type: (document.getElementById('session-type').textContent || '').trim(),
    title: (document.getElementById('header-title').textContent || '').trim(),
  }));
  expect(header.type.length, 'session type cell is empty').toBeGreaterThan(0);
  expect(header.title.length, 'header title is empty').toBeGreaterThan(0);

  // The leader's gap cell is a label ("Leader"), everyone else gets a value from
  // the snapshot's gap fields — so a rename there shows up as empty cells here.
  const rows = await readShownRows(page);
  const followers = rows.slice(1);
  expect(followers.length).toBeGreaterThan(0);
  for (const r of followers) {
    expect(r.gap.length, `no gap rendered for ${r.name || '(unnamed row)'}`).toBeGreaterThan(0);
  }
});
