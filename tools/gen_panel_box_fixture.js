#!/usr/bin/env node
// ============================================================================
// tools/gen_panel_box_fixture.js — regenerates the box-model golden vectors
// from tools/panel_box_model.js.
//
//   node tools/gen_panel_box_fixture.js
//
// Writes tests/fixtures/panel_box_parity.json: vectors for parseSides and
// layoutPanel, asserted against mxbmrp3/core/panel_box.h by
// tests/unit/panel_box_test.cpp and tests/integration/tests/box_terms_test.cpp.
// Regenerate ONLY when the model itself changes, and expect the C++ suite to
// go red until panel_box.h has been changed the same way.
//
// WHAT THIS FIXTURE DOES NOT PROVE, and the trap it set once: it pins the two
// implementations to EACH OTHER, not to what is right. Change panel_box.h and
// panel_box_model.js the same wrong way, regenerate, and both go green —
// which is how a themed panel came to reserve card border with no card drawn
// (card-off went 38x20 -> 41x25 in this file and nothing complained). When a
// regeneration moves numbers, the question to answer is "is the NEW number
// correct", and the fixture cannot answer it; the screenshot diff
// (companion_demo.sh) and the geometry cases in tests/integration can.
//
// The case list is hand-chosen rather than a cross product: each case is
// there to pin a specific rule (shrink-to-fit winner, non-collapsing seam,
// per-box columns, ceil slack, unthemed border collapse, button row widths),
// plus two metrics roots so the unit conversion is exercised off the shipped
// lattice too.
// ============================================================================
'use strict';
const fs = require('fs');
const path = require('path');
const BM = require('./panel_box_model.js');

const OUT = path.resolve(__dirname, '../tests/fixtures/panel_box_parity.json');

// ---- parseSides vectors ----------------------------------------------------
const sidesCases = [
  '', '2', '0.5', '2 4', '1 2 3', '1 2 3 4', '  2,4 ', '1,2,3,4',
  '2px 4', 'abc', '1e1 0.25', '-1 2', '0 0 0 0', '3.5',
].map(s => ({ in: s, out: BM.parseSides(s) }));

// ---- layoutPanel vectors ---------------------------------------------------
// Two lattices: the shipped one, and one far from it (unit well off 0.8333).
const M_SHIP = BM.metrics(0.020, 1.17335);
const M_ALT = BM.metrics(0.026, 1.5);

const S = BM.parseSides;
const box = (mg, bd, pd) => ({ margin: S(mg), border: S(bd), padding: S(pd) });

function spec(m, o) {
  return Object.assign({
    unit: m.unit,
    panel: { border: S(o.pb ?? '2'), padding: S(o.pp ?? '2') },
    title: box(o.tm ?? '0', o.tb ?? '1', o.tp ?? '0.5'),
    content: box(o.cm ?? '0.5', o.cb ?? '1', o.cp ?? '0'),
    button: box(o.bm ?? '0.5', o.bb ?? '1', o.bp ?? '0.5'),
    themed: o.themed ?? true, band: o.band ?? true, card: o.card ?? true,
    caption: o.caption === false ? null : { w: BM.CAPTION_CELLS, h: o.capH ?? m.cap },
    sections: o.bands ? [] : (o.secs ?? [3 * m.row]),
    cols: o.bands ? 0 : (o.cols ?? 30),
    bands: o.bands ?? [],
    buttons: o.btns ? { n: o.btns, w: o.btnW ?? BM.BUTTON_CHARS, h: m.row } : null,
    gap: o.gap ?? 0,
  });
}

const cases = [
  // The page's own "reset to shipped" state: a shipped theme, Timing shape.
  ['shipped-timing', spec(M_SHIP, { secs: [1 * M_SHIP.row, 3 * M_SHIP.row] })],
  // Single section, no band/caption — the plainest HUD.
  ['plain-hud', spec(M_SHIP, { band: false, caption: false })],
  // Unthemed: borders collapse to zero, air terms stay live.
  ['unthemed', spec(M_SHIP, { themed: false, secs: [2 * M_SHIP.row, 2 * M_SHIP.row] })],
  ['unthemed-buttons', spec(M_SHIP, { themed: false, btns: 3 })],
  // Caption without a band: bare row. The title BORDER collapses (no band art
  // to fill it) while its margin/padding stay live, so the column sits at the
  // panel's own inner edge plus whatever air the title box still asks for.
  ['caption-no-band', spec(M_SHIP, { band: false, capH: M_SHIP.row })],
  // Card off: the content BORDER collapses (no card art to fill it); the
  // content margin/padding still spend, so rows sit at the panel's inset plus
  // those. Themed, deliberately - this is the case that caught a themed panel
  // reserving card border with no card drawn.
  ['card-off', spec(M_SHIP, { card: false, secs: [1 * M_SHIP.row, 3 * M_SHIP.row] })],
  // Three sections: two seams, both the SUM of facing margins.
  ['three-sections', spec(M_SHIP, { secs: [M_SHIP.row, 3 * M_SHIP.row, 2 * M_SHIP.row] })],
  ['sections-touch', spec(M_SHIP, { cm: '0', secs: [M_SHIP.row, M_SHIP.row] })],
  // Buttons: margins working sideways; then wide enough to set the panel width.
  ['buttons-3', spec(M_SHIP, { btns: 3 })],
  ['buttons-set-width', spec(M_SHIP, { btns: 4, btnW: 14, cols: 16 })],
  ['button-1', spec(M_SHIP, { btns: 1 })],
  // A caption wide enough to win the ask (long caption widens the panel).
  ['caption-sets-width', spec(M_SHIP, { caption: true, cols: 8, capW: 0, secs: [M_SHIP.row] })],
  // Per-side shorthand: unequal sides, stretched corners flagged.
  ['four-sided', spec(M_SHIP, { pb: '1 2 3 4', pp: '2 1', tb: '0.5 1', cb: '1 0.5 1 2' })],
  ['vert-horiz', spec(M_SHIP, { pp: '2 4', cm: '0.5 1' })],
  // Title margin: grows the panel on both axes, band narrower by its own margin.
  ['title-margin', spec(M_SHIP, { tm: '1' })],
  ['title-margin-4', spec(M_SHIP, { tm: '0.5 1 0.5 2' })],
  // ONE SIDE AT A TIME, and this is the case class that was missing. Every other
  // case here sets a term VERTICALLY SYMMETRIC ('1', or '0.5 1 0.5 2' -- top and
  // bottom equal), so spending a box's TOP at its BOTTOM leaves every golden
  // unchanged and the fixture, the C++ unit test and the JS parity spec all pass
  // on it. Measured: layoutPanel's `titleTop = y + t.m.t` mutated to `t.m.b` was
  // invisible to all three. A term is only pinned to its own SIDE by a case where
  // that side is the only one set.
  ['title-margin-top',     spec(M_SHIP, { tm: '2 0 0 0' })],
  ['title-margin-bottom',  spec(M_SHIP, { tm: '0 0 2 0' })],
  ['title-margin-left',    spec(M_SHIP, { tm: '0 0 0 2' })],
  // ...and the RIGHT side needs the caption to be the widest ask, or it is slack
  // and the case pins nothing. caption-sets-width covers the caption's right
  // PADDING; its margin was {0,0,0,0} there, so dropping t.m.r from the ask was
  // green everywhere.
  ['title-margin-right-wins', spec(M_SHIP, { caption: true, cols: 8, capW: 0,
                                             tm: '0 2 0 0', secs: [M_SHIP.row] })],
  ['content-margin-top',   spec(M_SHIP, { cm: '2 0 0 0', secs: [M_SHIP.row, M_SHIP.row] })],
  ['content-margin-bottom',spec(M_SHIP, { cm: '0 0 2 0', secs: [M_SHIP.row, M_SHIP.row] })],
  ['content-pad-top',      spec(M_SHIP, { cp: '2 0 0 0' })],
  ['content-pad-bottom',   spec(M_SHIP, { cp: '0 0 2 0' })],
  ['button-margin-top',    spec(M_SHIP, { bm: '2 0 0 0', btns: 2 })],
  ['button-margin-bottom', spec(M_SHIP, { bm: '0 0 2 0', btns: 2 })],
  // Content padding on: rows move by exactly the term, both axes.
  ['content-pad', spec(M_SHIP, { cp: '1' })],
  ['content-pad-bump', spec(M_SHIP, { cp: '2' })],
  // Zero everything but the panel: a bare box.
  ['bare-panel', spec(M_SHIP, { pb: '0', pp: '0', band: false, caption: false,
                                cm: '0', cb: '0', cp: '0', secs: [M_SHIP.row] })],
  // Fractional terms off the row lattice.
  ['fractional', spec(M_SHIP, { pp: '1.5', tp: '0.25', cm: '0.75' })],
  // Big borders on a small panel (layout reserves them; clamping is the
  // painter's problem, deliberately not this engine's).
  ['border-heavy', spec(M_SHIP, { pb: '6', cols: 10, secs: [M_SHIP.row] })],
  // The alternative lattice: same shapes, different unit.
  ['alt-shipped-timing', spec(M_ALT, { secs: [1 * M_ALT.row, 3 * M_ALT.row] })],
  ['alt-buttons', spec(M_ALT, { btns: 2 })],
  ['alt-four-sided', spec(M_ALT, { pb: '1 2 3 4', tm: '1', cm: '0.5 1' })],
  ['alt-unthemed', spec(M_ALT, { themed: false })],
  // A minimum panel width (the centre stack's shared column): wider than the
  // asks it clamps and reports 'min'; narrower it does nothing.
  ['min-width-wins', Object.assign(spec(M_SHIP, { cols: 12 }), { minPanelW: 40 })],
  ['min-width-idle', Object.assign(spec(M_SHIP, { cols: 30 }), { minPanelW: 10 })],
  // Gap: junction-only air. Whole and fractional (the fractional one pins the
  // band→card junction living INSIDE the quantized caption advance), with
  // margins zeroed so the seam IS the gap, and unthemed/untitled variants.
  ['gap-2', spec(M_SHIP, { gap: 2, secs: [M_SHIP.row, 2 * M_SHIP.row], btns: 2 })],
  ['gap-only-seams', spec(M_SHIP, { gap: 1, tm: '0', cm: '0', bm: '0',
                                    secs: [M_SHIP.row, M_SHIP.row], btns: 2 })],
  ['gap-fractional', spec(M_SHIP, { gap: 0.7, secs: [2 * M_SHIP.row, 2 * M_SHIP.row] })],
  ['gap-untitled', spec(M_SHIP, { caption: false, band: false, gap: 1,
                                  secs: [M_SHIP.row, M_SHIP.row] })],
  ['gap-unthemed', spec(M_SHIP, { themed: false, gap: 1,
                                  secs: [M_SHIP.row, M_SHIP.row], btns: 1 })],
  // BANDS: a body that is more than one column. `split-two` is the settings
  // panel's shape (a narrow sidebar beside a taller content stack — the band is
  // as tall as the taller column, and the LAST column takes the leftover width);
  // `split-uneven` puts three sections against one so the band height comes from
  // the OTHER column; `split-then-stack` mixes a split band with a plain one, so
  // the flattened `sections` view and the band list have to disagree in exactly
  // the way the readers expect.
  ['split-two', spec(M_SHIP, { bands: [{ columns: [
      { cols: 16, sections: [6 * M_SHIP.row] },
      { cols: 30, sections: [2 * M_SHIP.row, 3 * M_SHIP.row] }] }] })],
  ['split-uneven', spec(M_SHIP, { gap: 1, bands: [{ columns: [
      { cols: 20, sections: [M_SHIP.row, M_SHIP.row, M_SHIP.row] },
      { cols: 20, sections: [M_SHIP.row] }] }] })],
  ['split-then-stack', spec(M_SHIP, { bands: [
      { columns: [{ cols: 14, sections: [2 * M_SHIP.row] },
                  { cols: 14, sections: [3 * M_SHIP.row] }] },
      { columns: [{ cols: 30, sections: [M_SHIP.row] }] }] })],
  // Unthemed, so the split is laid out on air terms alone.
  ['split-unthemed', spec(M_SHIP, { themed: false, bands: [{ columns: [
      { cols: 12, sections: [2 * M_SHIP.row] },
      { cols: 24, sections: [2 * M_SHIP.row] }] }] })],
  // The SECOND column unambiguously tallest: the ceil remainder must land in
  // ITS last section (slack goes to the column that set the band bottom), so
  // the clearance below the panel's real lowest card is the bottom chrome
  // exactly. Once at the shipped metrics and once at a ratio that leaves a
  // large fractional remainder — the settings-panel wobble this rule fixes was
  // invisible at the shipped ratio and only measurable away from it.
  ['split-tall-second', spec(M_SHIP, { bands: [{ columns: [
      { cols: 16, sections: [1 * M_SHIP.row] },
      { cols: 30, sections: [2 * M_SHIP.row, 3 * M_SHIP.row] }] }] })],
  ['split-tall-second-11', spec(BM.metrics(0.020, 1.1), (() => {
      const m = BM.metrics(0.020, 1.1);
      return { bands: [{ columns: [
          { cols: 16, sections: [1 * m.row] },
          { cols: 30, sections: [2 * m.row, 3 * m.row] }] }] };
  })())],
];
// 'caption-sets-width' needs a wide caption; patch its spec (capW isn't a knob
// of spec() because CAPTION_CELLS is the page's constant).
cases.find(c => c[0] === 'caption-sets-width')[1].caption = { w: 40, h: M_SHIP.cap };

const round = v => (typeof v === 'number' && isFinite(v)) ? Math.round(v * 1e9) / 1e9 : v;
const deep = o => JSON.parse(JSON.stringify(o, (k, v) => round(v)));

const layoutCases = cases.map(([name, sp]) => {
  const g = BM.layoutPanel(sp);
  return {
    name,
    spec: deep(sp),
    out: deep({
      hasTitle: g.hasTitle, hasCard: g.hasCard, nBtn: g.nBtn,
      widthSetBy: g.widthSetBy, columnSplit: g.columnSplit,
      artBot: g.artBot, panelInner: g.panelInner,
      panelInnerLeft: g.panelInnerLeft, innerW: g.innerW,
      titleTop: g.titleTop, titleH: g.titleH, titleBot: g.titleBot,
      titleSlack: g.titleSlack, bandDrawnBot: g.bandDrawnBot,
      titleLeft: g.titleLeft, titleW: g.titleW,
      cardLeft: g.cardLeft, cardW: g.cardW,
      sections: g.sections.map(s => ({ top: s.top, bot: s.bot, rowsTop: s.rowsTop, h: s.h })),
      bands: g.bands.map(bd => ({ top: bd.top, bot: bd.bot,
        columns: bd.columns.map(col => ({ left: col.left, w: col.w,
          sections: col.sections.map(s => ({ top: s.top, bot: s.bot, rowsTop: s.rowsTop, h: s.h })) })) })),
      seam: g.seam, btnTop: g.btnTop, btnH: g.btnH, btnW: g.btnW,
      btns: g.btns.map(b => ({ x: b.x, w: b.w, labelX: b.labelX })),
      panelH: g.panelH, slackY: g.slackY, panelCols: g.panelCols,
      captionX: g.captionX, rowsX: g.rowsX, buttonX: g.buttonX,
      rowsTop: g.rowsTop, contentH: g.contentH, cardTop: g.cardTop, cardBot: g.cardBot,
      captionY: g.captionY, squareBorders: g.squareBorders,
      overTopY: g.overTopY, overX: g.overX,
    }),
  };
});

const fixture = {
  _comment: 'GENERATED by tools/gen_panel_box_fixture.js — do not hand-edit. ' +
            'Asserted against mxbmrp3/core/panel_box.h by ' +
            'tests/unit/panel_box_test.cpp and tests/integration/tests/box_terms_test.cpp.',
  parseSides: sidesCases,
  layoutPanel: layoutCases,
};

fs.writeFileSync(OUT, JSON.stringify(fixture, null, 1) + '\n');
console.log('wrote', OUT, '—', sidesCases.length, 'parseSides cases,',
            layoutCases.length, 'layoutPanel cases');
