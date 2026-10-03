// ============================================================================
// tests/web/tests/status_lines.spec.js
// The overlay's own status lines in the event log. A green "Connected" is a
// confirmation, not news: it expires on its own (STATUS_LINE_TTL_MS). A red
// error line stays until a real event pushes it out of the maxEvents budget.
//
// THE BUG THIS PINS: on a broadcaster's stream the green "Connected (MXBMRP3
// v...)" sat at the bottom of the overlay for the whole pre-race, because the
// events the default filters let through (session, fastest lap, finishes...)
// do not happen while a field spawns in, so nothing arrived to push it out,
// and no setting removed it. The TTL is shrunk in-page so the test needn't
// wait the real ten seconds.
//
// THE SECOND BUG: with the TTL, a reconnect left the red line ALONE. "Connection
// lost" never expires and "Connected" did, so ten seconds after the stream came
// back the only status line said it was gone. A green line now answers the red
// ones before it: they expire together. A red line after the green one is new
// news and still stays (the first test).
// ============================================================================
const { test, expect } = require('@playwright/test');

test('a green status line expires on its own; a red one stays until pushed out', async ({ page }) => {
  // No ?demo: the demo replays its own synthetic clock, which sorts real-time
  // status lines out of the budget. Served by a static server with no plugin
  // behind it, the overlay logs its own connection lines; the two below are
  // appended on top and are the only ones asserted.
  await page.goto('/index.html');
  await expect(page.locator('#event-log')).toBeAttached();

  await page.evaluate(() => {
    STATUS_LINE_TTL_MS = 300;        // real value: 10 s
    appendStatusLine('Connected (test)', 'ok');
    appendStatusLine('Connection lost (test)', 'error');
  });
  const status = page.locator('#event-log .event-entry[data-status="1"]');
  const green = status.filter({ hasText: 'Connected (test)' });
  const red = status.filter({ hasText: 'Connection lost (test)' });
  await expect(green).toHaveCount(1);
  await expect(red).toHaveCount(1);

  // The green one goes on the tick appendStatusLine scheduled; the red one does not.
  await expect(green).toHaveCount(0, { timeout: 3000 });
  await expect(red).toHaveCount(1);
});

test('a green line answers the red ones before it: they expire together', async ({ page }) => {
  await page.goto('/index.html');
  await expect(page.locator('#event-log')).toBeAttached();

  await page.evaluate(() => {
    STATUS_LINE_TTL_MS = 300;
    appendStatusLine('Connection lost (test)', 'error');
    appendStatusLine('Connected (test)', 'ok');
  });
  const status = page.locator('#event-log .event-entry[data-status="1"]');
  const green = status.filter({ hasText: 'Connected (test)' });
  const red = status.filter({ hasText: 'Connection lost (test)' });
  await expect(green).toHaveCount(1);
  await expect(red).toHaveCount(1);

  await expect(green).toHaveCount(0, { timeout: 3000 });
  await expect(red).toHaveCount(0);
});
