// ============================================================================
// tests/web/playwright.config.js
// Serves the overlay's static files (../mxbmrp3_data/web) with a plain HTTP
// server and drives its built-in ?demo mode in headless Chromium. No game, no
// plugin, no network — the demo replays synthetic snapshots through the same
// render() the live SSE stream feeds, so the tests exercise the real client
// rendering (tower, positions, gaps, session/clock). See README.md.
// ============================================================================
const path = require('path');
const { defineConfig, devices } = require('@playwright/test');

const WEB_DIR = path.resolve(__dirname, '../../mxbmrp3_data/web');
const PORT = 8099;

module.exports = defineConfig({
  testDir: './tests',
  timeout: 30_000,
  expect: { timeout: 10_000 },
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  // One retry on CI so a transient flake doesn't fail the run outright — and
  // retain-on-failure so a genuine failure always leaves a trace to debug
  // (previously trace was 'on-first-retry' with retries unset, so no CI failure
  // ever captured one).
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? [['github'], ['list']] : 'list',
  use: {
    baseURL: `http://127.0.0.1:${PORT}`,
    trace: 'retain-on-failure',
  },
  projects: [
    // MXB_CHROMIUM: optional path to a system/preinstalled Chromium, for
    // sandboxed environments where Playwright's pinned browser download is
    // blocked (e.g. a preinstalled /opt/pw-browsers build). Unset = default.
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        launchOptions: process.env.MXB_CHROMIUM
          ? { executablePath: process.env.MXB_CHROMIUM }
          : {},
      },
    },
  ],
  // Static file server for the overlay. python3 is always available on the
  // runners; -1 headers aren't needed since each test loads fresh.
  webServer: {
    command: `python3 -m http.server ${PORT} --directory "${WEB_DIR}"`,
    url: `http://127.0.0.1:${PORT}/index.html`,
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
