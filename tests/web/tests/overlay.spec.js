// ============================================================================
// tests/web/tests/overlay.spec.js
// Drives the overlay's ?demo mode (a synthetic 22-rider warmup + race) and
// asserts the client renders it correctly — the layer the C++ integration tests
// can't reach (they assert the plugin's JSON; these assert the browser DOM the
// JSON is rendered into). ?speed=<n> fast-forwards the synthetic clock so the
// warmup->race transition is reached in seconds.
//
// Note: the tower keeps one row element per rider in a STABLE DOM order and
// moves each to its slot with a translateY transform (for smooth position-change
// animation). So DOM order != visual order — assertions that care about ranking
// sort rows by their on-screen Y and find the leader by its position value, not
// by DOM index. Transitions are disabled first so a mid-animation read is stable.
// ============================================================================
const { test, expect } = require('@playwright/test');

// Roster surnames the demo renders. Kept short enough to survive the name
// column's default truncation, so a substring match is stable.
const KNOWN_NAMES = ['Roczen', 'Cooper', 'Webb', 'Prado', 'Barcia', 'Sexton'];

// Kill CSS transitions so translateY row slots are read at their target, not
// mid-animation — makes the visual-order read deterministic.
async function freezeAnimations(page) {
  await page.addStyleTag({ content: '*{transition:none !important;animation:none !important}' });
}

// One snapshot of every tower row: its rank, name, gap, on-screen Y, and whether
// it's shown. Riders below the Max-Riders cutoff stay in the DOM but are
// display:none (ty/y collapse to 0), so ranking assertions filter to `shown`.
function readRows(rows) {
  return rows.evaluateAll((els) =>
    els.map((e) => ({
      pos: Number((e.querySelector('.col-pos')?.textContent || '').trim()),
      name: (e.querySelector('.col-name')?.textContent || '').trim(),
      gap: (e.querySelector('.col-gap')?.textContent || '').trim(),
      y: e.getBoundingClientRect().top,
      shown: getComputedStyle(e).display !== 'none',
    })),
  );
}

test('tower populates with a full, correctly-ordered field and no page errors', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  await page.goto('/index.html?demo&speed=30');

  const rows = page.locator('#standings-body .standings-row');
  await expect(rows.first()).toBeVisible();
  // The synthetic field is 22 riders; the tower shows the bulk of them.
  await expect.poll(() => rows.count()).toBeGreaterThanOrEqual(15);

  await freezeAnimations(page);
  const info = (await readRows(rows)).filter((d) => d.shown);   // the visible tower
  expect(info.length).toBeGreaterThanOrEqual(15);

  // The visible tower is the top N: ranks are contiguous 1..N, each once.
  const ranksSorted = info.map((d) => d.pos).sort((a, b) => a - b);
  expect(ranksSorted).toEqual(ranksSorted.map((_, i) => i + 1));

  // Read top-to-bottom on screen, the positions ascend 1..N.
  const byScreen = [...info].sort((a, b) => a.y - b.y).map((d) => d.pos);
  expect(byScreen).toEqual(byScreen.map((_, i) => i + 1));

  // Every row has a name, and the real roster names flow through render().
  expect(info.every((d) => d.name.length > 0)).toBe(true);
  const joined = info.map((d) => d.name).join(' ');
  expect(KNOWN_NAMES.filter((s) => joined.includes(s)).length).toBeGreaterThanOrEqual(2);

  // Session identity + a clock (MM:SS during the session, or an overtime label).
  await expect(page.locator('#session-type')).not.toHaveText('');
  await expect(page.locator('#session-time')).toHaveText(/\d\d:\d\d|LAP|TO GO|CHECKERED/);

  // The overlay JS ran clean — no uncaught exceptions.
  expect(pageErrors).toEqual([]);
});

test('reaches the race phase and shows leader-relative gaps', async ({ page }) => {
  await page.goto('/index.html?demo&speed=40');

  // The demo runs a warmup first, then the timed race — wait for the transition.
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  const rows = page.locator('#standings-body .standings-row');
  await expect.poll(() => rows.count()).toBeGreaterThanOrEqual(15);

  // In a race the leader (pos 1) row's gap column is the leader label, not a time.
  const info = await readRows(rows);
  const leader = info.find((d) => d.pos === 1);
  expect(leader, 'a rider is classified P1').toBeTruthy();
  expect(leader.gap).toBe('Leader');
});

test('battle card shows real-time (live) intervals when the overlay toggle is on', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // The bottom slot is shared and mutually exclusive; the demo's constant best-lap
  // churn otherwise keeps the fastest-lap panel in it and starves the battle panel.
  // CONFIG is overridable via localStorage (mxbmrp3_settings), so disable the
  // competing panels before boot to reserve the slot for the battle panel. Live gaps
  // are OFF by default now, so enable them explicitly for this test.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings',
      JSON.stringify({ fastLap: false, bestLap: false, sectors: false, tail: false,
                       battleLiveGaps: true }));
  });

  // Speed 40 reaches the race quickly; the camera then cycles the lead pack (close
  // gaps), so the director frames a battle repeatedly. The demo marks lead-lap
  // riders liveGapValid and CONFIG.battleLiveGaps defaults on, so every battle-card
  // interval (the non-front rows) resolves to the live path.
  await page.goto('/index.html?demo&speed=40');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  // The battle panel holds only while the director frames it — at demo speed that's
  // a brief flash a poll can miss. Latch the first live interval via a MutationObserver
  // so a transient appearance is still caught, and capture its text + a same-card
  // front headline for the sanity checks below.
  await page.evaluate(() => {
    window.__liveBattle = null;
    const grab = () => {
      if (window.__liveBattle) return;
      const el = document.querySelector('#battle-list .battle-value.live');
      if (!el) return;
      const front = document.querySelector('#battle-list .battle-card .battle-value:not(.live)');
      window.__liveBattle = {
        text: el.textContent.trim(),
        hasFront: !!front,
        frontText: front ? front.textContent.trim() : '',
      };
    };
    new MutationObserver(grab).observe(document.getElementById('battle-list'),
      { childList: true, subtree: true, characterData: true });
    grab();
  });

  await expect
    .poll(() => page.evaluate(() => window.__liveBattle), { timeout: 45_000 })
    .not.toBeNull();

  const seen = await page.evaluate(() => window.__liveBattle);
  // The live interval reads as a gap time (a signed seconds value), not empty/NaN.
  expect(seen.text).toMatch(/[+\-]?\d/);
  // The front-of-battle headline is a position ordinal (e.g. "1ST"), never a live gap.
  expect(seen.hasFront).toBe(true);
  expect(seen.frontText).toMatch(/[A-Z]/);

  expect(pageErrors).toEqual([]);
});

test('focus card shows the position as a large ordinal headline on the right', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  await page.goto('/index.html?demo&speed=25');

  // The focus card shows for the spectated rider (not during a director battle shot).
  const card = page.locator('#focus-card');
  await expect(card).toHaveClass(/focus-in/, { timeout: 30_000 });
  await freezeAnimations(page);

  const info = await page.evaluate(() => {
    const fc = document.getElementById('focus-card');
    const val = fc.querySelector('.focus-value');
    const sub = fc.querySelector('.battle-sub');   // a detail row, for a size comparison
    const cr = fc.getBoundingClientRect();
    const vr = val.getBoundingClientRect();
    return {
      text: val.textContent.trim(),
      position: getComputedStyle(val).position,
      valuePx: parseFloat(getComputedStyle(val).fontSize),
      subPx: parseFloat(getComputedStyle(sub).fontSize),
      rightGap: cr.right - vr.right,               // hugs the right edge
      centerY: vr.top + vr.height / 2 - cr.top,    // vertical centre offset within the card
      cardHeight: cr.height,
    };
  });

  // A position ordinal (e.g. "1ST" / "12TH"), uppercased.
  expect(info.text).toMatch(/^\d+(ST|ND|RD|TH)$/);
  // Large: the headline font is bigger than a detail row's.
  expect(info.valuePx).toBeGreaterThan(info.subPx);
  // Middle-right: absolutely positioned, hugging the right edge, vertically centred
  // in the upper card area (above the bottom identity row), not pinned to the bottom.
  expect(info.position).toBe('absolute');
  expect(info.rightGap).toBeLessThan(30);
  expect(info.centerY).toBeLessThan(info.cardHeight * 0.75);

  expect(pageErrors).toEqual([]);
});

test('battle card falls back to official splits when Live Gaps is off', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // Same slot-freeing override, plus the Live Gaps toggle OFF.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings',
      JSON.stringify({ fastLap: false, bestLap: false, sectors: false, tail: false,
        battleLiveGaps: false }));
  });

  await page.goto('/index.html?demo&speed=40');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  // Latch the first battle card that shows an INTERVAL value (a gap like "+0.8",
  // distinguished from the front ordinal "1ST" by the decimal), recording whether
  // any interval carried the `.live` marker at that moment — with the toggle off it
  // must not.
  await page.evaluate(() => {
    window.__offBattle = null;
    const grab = () => {
      if (window.__offBattle) return;
      const vals = Array.from(document.querySelectorAll('#battle-list .battle-value'));
      const interval = vals.find((e) => /[+\-]?\d+\.\d/.test(e.textContent));   // a split, not an ordinal
      if (!interval) return;
      window.__offBattle = {
        intervalText: interval.textContent.trim(),
        liveCount: document.querySelectorAll('#battle-list .battle-value.live').length,
      };
    };
    new MutationObserver(grab).observe(document.getElementById('battle-list'),
      { childList: true, subtree: true, characterData: true });
    grab();
  });

  await expect
    .poll(() => page.evaluate(() => window.__offBattle), { timeout: 45_000 })
    .not.toBeNull();

  const off = await page.evaluate(() => window.__offBattle);
  expect(off.intervalText).toMatch(/[+\-]?\d+\.\d/);   // an official split rendered
  expect(off.liveCount).toBe(0);                        // but nothing marked live

  expect(pageErrors).toEqual([]);
});

test('battle panel: a cut to a different battle slides fully out then in — one card, one header', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // Reserve the shared bottom slot for the battle panel (disable the competing boards),
  // as the other battle tests do. The demo's race director frames DISTINCT battles and
  // cuts between them. ?race skips the warmup so the race (and its battles) are up
  // immediately.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings',
      JSON.stringify({ fastLap: false, bestLap: false, sectors: false, tail: false }));
  });
  await page.goto('/index.html?demo&race&speed=40');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  // A battle change is a full panel change like every other slot panel: the whole
  // #battle-panel slides OUT (battle-out → fully hidden, revealing the tower) and a fresh one
  // slides IN (battle-in) — NOT an in-place two-card reel. Sample continuously and record the
  // worst case: track + header counts (must never exceed one — the "one card, one header"
  // guarantee), whether the reel overlay ever appeared, whether the panel showed a battle, slid
  // fully out, and came back (a complete out→hidden→in cycle).
  await page.evaluate(() => {
    window.__b = { maxTracks: 0, maxTitles: 0, maxTitlesOutside: 0, leavingSeen: false,
                   shownSeen: false, fullyHiddenSeen: false, cycles: 0, phase: 'init' };
    const sample = () => {
      const b = window.__b;
      const panel = document.getElementById('battle-panel');
      b.maxTracks = Math.max(b.maxTracks, document.querySelectorAll('#battle-list .battle-track').length);
      b.maxTitles = Math.max(b.maxTitles, document.querySelectorAll('#battle-panel .battle-title').length);
      b.maxTitlesOutside = Math.max(b.maxTitlesOutside,
        Array.from(document.querySelectorAll('#battle-panel .battle-title'))
          .filter((t) => !t.closest('.battle-track')).length);
      if (document.querySelector('.battle-track-leaving')) b.leavingSeen = true;   // the removed reel
      const shown = panel.classList.contains('battle-in') && !panel.classList.contains('hidden');
      const fullyHidden = panel.classList.contains('hidden');
      if (shown) b.shownSeen = true;
      if (fullyHidden) b.fullyHiddenSeen = true;
      // Count complete shown → fully-hidden → shown cycles: the panel fully slides out
      // (revealing the tower) before the next battle arrives — never a half-slide reel.
      if (shown && b.phase !== 'shown') { if (b.phase === 'hidden') b.cycles++; b.phase = 'shown'; }
      else if (fullyHidden && b.phase === 'shown') b.phase = 'hidden';
    };
    window.__bTimer = setInterval(sample, 60);
    sample();
  });

  // Wait until we've observed at least one full shown → fully-hidden → shown cycle.
  await expect
    .poll(() => page.evaluate(() => window.__b.cycles), { timeout: 45_000 })
    .toBeGreaterThanOrEqual(1);
  const b = await page.evaluate(() => { clearInterval(window.__bTimer); return window.__b; });

  // The core guarantee: only ever ONE battle card and ONE header on screen — never the
  // in-place two-track reel (which showed two headers and never revealed the tower).
  expect(b.maxTracks).toBeLessThanOrEqual(1);
  expect(b.maxTitles).toBeLessThanOrEqual(1);
  expect(b.leavingSeen).toBe(false);          // the old reel overlay never appears
  expect(b.maxTitlesOutside).toBe(0);         // the header lives in the track, no static sibling
  // The panel showed a battle, then FULLY slid out (tower revealed) before the next arrived.
  expect(b.shownSeen).toBe(true);
  expect(b.fullyHiddenSeen).toBe(true);

  expect(pageErrors).toEqual([]);
});

test('tower accent strip stays on the tracked rider only, never the whole battle group', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));
  await page.goto('/index.html?demo&race&speed=40');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  // The left accent strip (the "who's on camera" marker) must sit on AT MOST one tower row
  // at any instant — the tracked rider — even while the director is framing a multi-rider
  // battle. The battle GROUP is shown by the bottom panel, not by wrapping the tower
  // highlight across every rider in the fight. Install a sampler and record the worst case.
  await page.evaluate(() => {
    window.__hl = { maxCamera: 0, partnerSeen: false, battleSamples: 0 };
    const sample = () => {
      const cam = document.querySelectorAll('#standings-body .camera-row').length;
      if (cam > window.__hl.maxCamera) window.__hl.maxCamera = cam;
      // The removed battle-group highlight (director-partner) must never come back.
      if (document.querySelector('#standings-body .director-partner')) window.__hl.partnerSeen = true;
      // Count moments where the director is actively framing a battle (panel up), so the
      // assertion is known to have covered the case the old multi-row highlight fired on.
      if (!document.getElementById('battle-panel').classList.contains('hidden')) window.__hl.battleSamples++;
    };
    window.__hlTimer = setInterval(sample, 80);
    sample();
  });

  // Sample until we've actually observed the director framing a battle (the old code would
  // have lit the whole group here); the sampler runs throughout.
  await expect
    .poll(() => page.evaluate(() => window.__hl.battleSamples), { timeout: 45_000 })
    .toBeGreaterThan(0);
  const hl = await page.evaluate(() => { clearInterval(window.__hlTimer); return window.__hl; });

  expect(hl.maxCamera).toBeLessThanOrEqual(1);   // one tracked rider highlighted, never the group
  expect(hl.partnerSeen).toBe(false);            // the group-wrapping highlight never appears
  expect(pageErrors).toEqual([]);
});

test('demo ?warmup locks the warmup phase; ?race skips it', async ({ page }) => {
  // ?warmup stays in the warmup and loops it — at speed 50 the 8-minute warmup would
  // otherwise elapse and hand off to the race within a second, so still being in "Warmup"
  // a few seconds later proves the lock.
  await page.goto('/index.html?demo&warmup&speed=50');
  await expect(page.locator('#session-type')).toContainText('Warmup', { timeout: 15_000 });
  await page.waitForTimeout(3_000);
  await expect(page.locator('#session-type')).toContainText('Warmup');

  // ?race jumps straight into the race — no warmup to sit through.
  await page.goto('/index.html?demo&race&speed=50');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 15_000 });
});

test('session charts panel renders a carousel of SVG charts when forced', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // Reserve the shared bottom slot (disable the competing panels) and enable every
  // chart page so the carousel has all four (lap / trace / gap / pace). The panel is
  // forced via the demo's force hook so the test is deterministic (no waiting for the
  // finish trigger). ?race skips the warmup so laps accrue immediately.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({
      fastLap: false, bestLap: false, sectors: false, tail: false, battle: false,
      charts: true, chartLap: true, chartTrace: true, chartGap: true, chartPace: true,
    }));
  });
  await page.goto('/index.html?demo&race&speed=20');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  // Wait until some laps exist, then force the charts panel in.
  await page.waitForFunction(() => typeof window.mxbmrp3ForceSlot === 'function', { timeout: 30_000 });
  await expect
    .poll(() => page.evaluate(() => ((window.mxbmrp3LastData && window.mxbmrp3LastData.laps) || []).some((l) => l.t && l.t.length >= 2)),
      { timeout: 30_000 })
    .toBe(true);
  await page.evaluate(() => window.mxbmrp3ForceSlot('charts'));

  // The panel slides in and holds an inline SVG chart with rider polylines and tags.
  const panel = page.locator('#charts-panel');
  await expect(panel).toHaveClass(/charts-in/, { timeout: 10_000 });
  await expect(panel.locator('.charts-svg').first()).toBeVisible();
  await expect.poll(() => panel.locator('.charts-svg polyline').count()).toBeGreaterThan(0);

  // One carousel page per enabled chart (all four here), each an SVG with its own
  // title as the first row (the header slides in with its page, not a fixed title).
  const info = await page.evaluate(() => ({
    pages: document.querySelectorAll('#charts-panel .charts-page').length,
    svgs: document.querySelectorAll('#charts-panel .charts-page > svg.charts-svg').length,
    titles: Array.from(document.querySelectorAll('#charts-panel .charts-page > .charts-title'))
      .map((t) => t.textContent.trim()),
  }));
  expect(info.pages).toBe(4);
  expect(info.svgs).toBe(4);
  // Every page carries its own non-empty title row.
  expect(info.titles.length).toBe(4);
  expect(info.titles.every((t) => t.length > 0)).toBe(true);

  // The carousel advances through its pages on its own timer — the track's
  // horizontal transform steps away from the first page.
  await expect
    .poll(() => page.evaluate(() => {
      const tr = getComputedStyle(document.querySelector('#charts-panel .charts-track')).transform;
      return tr && tr !== 'none' && tr !== 'matrix(1, 0, 0, 1, 0, 0)';
    }), { timeout: 15_000 })
    .toBe(true);

  expect(pageErrors).toEqual([]);
});

test('session charts panel auto-shows when the race leader finishes', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // No force here: reserve the slot and let the demo race run to the flag. The only
  // way the charts panel can take the slot is its leader-finish auto-trigger, so a
  // charts-in class proves the auto-show fired. Fast speed reaches the finish quickly.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({
      fastLap: false, bestLap: false, sectors: false, tail: false, battle: false, charts: true,
    }));
  });
  await page.goto('/index.html?demo&race&speed=30');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  await expect(page.locator('#charts-panel')).toHaveClass(/charts-in/, { timeout: 45_000 });
  await expect.poll(() => page.locator('#charts-panel .charts-svg polyline').count()).toBeGreaterThan(0);

  expect(pageErrors).toEqual([]);
});

test('session charts panel forced before any laps upgrades to real charts once data arrives', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // Reserve the slot and run the race SLOWLY so there's a window at the start with no
  // completed laps. Force charts immediately — build() has no data, so it shows the
  // shared "No data" placeholder row. The panel must not park there: once laps arrive its
  // refresh() upgrades the placeholder in place into the real carousel (we never
  // re-force), so polylines appear. Before the fix the placeholder stayed stuck.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({
      fastLap: false, bestLap: false, sectors: false, tail: false, battle: false, charts: true,
    }));
  });
  await page.goto('/index.html?demo&race&speed=2');
  // Wait for the first render (standings exist, so force() won't no-op on !lastData)
  // but before any lap has completed, then force — so build() genuinely has no data.
  await page.waitForFunction(() => {
    const d = window.mxbmrp3LastData;
    return typeof window.mxbmrp3ForceSlot === 'function' &&
      d && d.standings && d.standings.length && !(d.laps || []).some((l) => l.t && l.t.length);
  }, { timeout: 30_000 });
  await page.evaluate(() => window.mxbmrp3ForceSlot('charts'));

  // The placeholder shows first (the shared "No data" row, no real chart axis).
  await expect(page.locator('#charts-panel')).toHaveClass(/charts-in/, { timeout: 10_000 });
  await expect.poll(() => page.locator('#charts-panel .board-row.board-empty').count()).toBeGreaterThan(0);

  // Without re-forcing, it upgrades IN PLACE once lap data arrives: the placeholder
  // row is gone and a real chart (axis labels) is drawn, still on screen. Before the
  // fix the placeholder parked here forever (row stays, axis never appears).
  await expect
    .poll(() => page.evaluate(() => {
      const p = document.getElementById('charts-panel');
      return p.classList.contains('charts-in') &&
        document.querySelectorAll('#charts-panel .board-row.board-empty').length === 0 &&
        document.querySelectorAll('#charts-panel .charts-svg .chart-axis').length > 0;
    }), { timeout: 20_000 })
    .toBe(true);

  expect(pageErrors).toEqual([]);
});

test('leader-finish charts auto-show is not lost to a manually-forced panel', async ({ page }) => {
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(String(e)));

  // The user scenario: a broadcaster has a panel manually forced up at the flag.
  // Charts must NOT override that force (it stays below Infinity priority), but its
  // one-shot leader-finish trigger must not be DROPPED either — it should defer and
  // slide in the moment the forced panel clears. Before the fix the event was
  // consumed while blocked and charts never appeared.
  //
  // We give the forced fastlap board a very long Panel Time so it is guaranteed to
  // still be masking (at top Infinity priority) at the instant the leader finishes,
  // then hide it manually right after — reproducing the block without racing the
  // exact finish edge.
  await page.addInitScript(() => {
    localStorage.setItem('mxbmrp3_settings', JSON.stringify({
      fastLap: true, bestLap: false, sectors: false, tail: false, battle: false, charts: true,
      slotDuration: 120, slotRest: 0,
    }));
  });
  await page.goto('/index.html?demo&race&speed=30');
  await expect(page.locator('#session-type')).toContainText('Race', { timeout: 30_000 });

  // Once laps exist, force the fastlap board so it occupies the slot at Infinity
  // priority for the rest of the race (long Panel Time keeps it up through the flag).
  await page.waitForFunction(() => {
    const d = window.mxbmrp3LastData;
    return typeof window.mxbmrp3ForceSlot === 'function' &&
      d && (d.laps || []).some((l) => l.t && l.t.length >= 1);
  }, { timeout: 30_000 });
  await page.evaluate(() => window.mxbmrp3ForceSlot('fastlap'));
  await expect(page.locator('#fastlap-panel')).toHaveClass(/fastlap-in/, { timeout: 10_000 });

  // Wait for the race leader (P1) to cross the line — the charts auto-show trigger
  // fires here, while the forced fastlap board is still masking (so it is blocked).
  await page.waitForFunction(() => {
    const st = window.mxbmrp3LastData && window.mxbmrp3LastData.standings;
    if (!st) return false;
    const leader = st.find((r) => r.pos === 1);
    return !!(leader && leader.finished);
  }, { timeout: 60_000 });

  // Charts must not have overridden the manual force while it was up.
  expect(await page.locator('#charts-panel').getAttribute('class')).not.toMatch(/charts-in/);

  // Now clear the forced panel. The deferred leader-finish show must fire the moment
  // the slot frees — charts slides in with real chart content. Before the fix the
  // event was already consumed, so charts stayed hidden here.
  await page.evaluate(() => {
    const fl = slotPanels.find((p) => p.name === 'fastlap');
    if (fl) fl.hide();
  });
  await expect(page.locator('#charts-panel')).toHaveClass(/charts-in/, { timeout: 10_000 });
  await expect.poll(() => page.locator('#charts-panel .charts-svg polyline').count()).toBeGreaterThan(0);

  expect(pageErrors).toEqual([]);
});
