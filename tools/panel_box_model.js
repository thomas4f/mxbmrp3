// ============================================================================
// tools/panel_box_model.js — the box model's arithmetic, on its own.
//
// This is the SOURCE the golden vectors come from. tools/gen_panel_box_fixture.js
// runs it to write tests/fixtures/panel_box_parity.json, and that fixture is
// what tests/unit/panel_box_test.cpp and tests/integration/tests/box_terms_test.cpp
// assert mxbmrp3/core/panel_box.h against. So the arithmetic here and the C++
// engine's must stay term for term: change one, regenerate, and the C++ suite
// tells you whether the other side agreed.
//
// It began as the shared core of an interactive page (panel-box-model.html)
// that let the geometry be drawn and argued about before any of it was built.
// The page and its own parity spec are gone — the model is settled and nothing
// shipped read them — but the generator is not scaffolding: without it the
// fixture is 8,000 lines of JSON nobody can legitimately change.
//
// WHAT THE FIXTURE DOES NOT PROVE, and the trap it set once: it pins the two
// implementations to EACH OTHER, not to what is right. Change panel_box.h and
// this file the same wrong way, regenerate, and everything goes green — which
// is how a themed panel came to reserve card border with no card drawn. A
// change to the model wants a test of the BEHAVIOUR too, not just a regenerated
// fixture.
//
// TWO LAYERS, deliberately:
//
//   layoutPanel(spec)  the general core — boxes in, geometry out. Knows
//                      nothing about fonts, tiers, TimingHud or captions'
//                      text; children arrive as content SIZES. This is the
//                      function panel_box.h mirrors, term for term.
//   geom(m, opt)       the page's wrapper — maps the page's knobs (font
//                      tiers, SECTION_ROWS, caption text) into a spec, calls
//                      layoutPanel, and decorates the result with the
//                      page-only glyph fields (big/tier/cell/ink).
//
// UNITS, same as the page and the plugin: horizontal values are x-cells,
// vertical values are y-cells, and a box term's top/bottom sides are its
// x-cell count converted by `unit` (= cellW*aspect/cellH) so one number is
// square on screen — the same conversion a theme's slice size already makes.
//
// UMD-lite: attaches to window as BoxModel for the page, exports via
// module.exports for node (the fixture generator and the Playwright spec).
// ============================================================================
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.BoxModel = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const CHARW = 0.275, PER_CHAR = 1, PER_ROW = 2, ASPECT = 16 / 9, SW = 1920, SH = 1080;

  // TimingHud's own shape, which is why the page's panel is the reference: one
  // big-value row in the first card, the comparison rows in the second. A
  // 1-section panel is every simpler HUD; a 3-section one is Performance.
  const SECTION_ROWS = { 1: [3], 2: [1, 3], 3: [1, 3, 2] };
  const BUTTON_CHARS = 7;          // "cancel" plus a space either side
  // "Timing" at the TITLE tier: 6 characters, and the large font is 1.5x, so
  // 1.5 cells of width each. A caption is content like any other, so it makes
  // the panel wide enough for itself.
  const CAPTION_CELLS = 6 * 1.5;

  function metrics(F, LH) {
    const cellW = F * CHARW / PER_CHAR;    // fraction of screen WIDTH
    const cellH = F * LH / PER_ROW;        // fraction of screen HEIGHT
    return {
      F, LH, cellW, cellH,
      cellWpx: cellW * SW, cellHpx: cellH * SH,
      unit: cellW * ASPECT / cellH,        // vertical cells spanned by one slice unit
      row: F * LH / cellH,                 // 2 cells
      cap: F / cellH,                      // caption glyph, in cells
      // THE FONT TIERS, and both numbers per tier, because they are not the same
      // number. LayoutMetrics::derive(): lineHeightLarge = rowL * lineHeightNormal,
      // so the ROW a tier occupies is its own multiple of the normal row -- and it
      // is deliberately a DIFFERENT multiple from the text: -l text is 1.5x but
      // sits in a 2x row, "because a title wants air its glyphs do not".
      tiers: {
        normal: { mult: 1.0, size: F / cellH,       row: 1.0 * F * LH / cellH },
        large:  { mult: 1.5, size: 1.5 * F / cellH, row: 2.0 * F * LH / cellH },
        xl:     { mult: 2.0, size: 2.0 * F / cellH, row: 2.0 * F * LH / cellH },
      },
      // A GLYPH'S INK, not its cell. Measured off the shipped .fnt by
      // test_font_metrics.cpp: the cap/digit band runs ~0.63 of the cell and is
      // CENTRED in it, because mxbmrp3_fontgen normalises every shipped font that
      // way. inkCenter mirrors LayoutMetrics::inkCenterRatio.
      inkH: 0.63, inkCenter: 0.5,
      bigRow: F * LH / cellH,
      titlePad: 0.5, basePadX: 2, basePadY: 2
    };
  }

  // CSS shorthand, cells: `2` all sides · `2 4` vertical horizontal ·
  // `1 2 3` top horizontal bottom · `1 2 3 4` top right bottom left.
  function parseSides(str) {
    const n = String(str).trim().split(/[\s,]+/).map(Number).filter(v => isFinite(v));
    if (n.length === 0) return { t: 0, r: 0, b: 0, l: 0 };
    if (n.length === 1) return { t: n[0], r: n[0], b: n[0], l: n[0] };
    if (n.length === 2) return { t: n[0], r: n[1], b: n[0], l: n[1] };
    if (n.length === 3) return { t: n[0], r: n[1], b: n[2], l: n[1] };
    return { t: n[0], r: n[1], b: n[2], l: n[3] };
  }

  // ---- the general core ----------------------------------------------------
  // spec = {
  //   unit,                              // y-cells per x-cell distance
  //   panel:   { border, padding },      // RAW sides (x-cells), no margin: see page
  //   title:   { margin, border, padding },
  //   content: { margin, border, padding },
  //   button:  { margin, border, padding },
  //   themed, band, card,                // switches (borders are zero unthemed)
  //   caption: null | { w, h },          // content size: w x-cells, h y-cells
  //   sections: [h0, h1, ...],           // content heights, y-cells (>= 1 entry)
  //   cols,                              // section content width, x-cells
  //   bands: [{ columns: [{ cols, sections }] }],   // the GENERAL body; a
  //     band is a horizontal group of columns, each with its own stack of
  //     sections. `sections`/`cols` above is the one-column shorthand and
  //     builds exactly one band of one column. Set one or the other.
  //   buttons: null | { n, w, h },       // n boxes of content w x-cells, h y-cells
  //   minPanelW,                         // optional: minimum PANEL width, x-cells
  //   gap,                               // optional: junction-only air, x-cells
  // }
  //
  // minPanelW exists for panels that share a column and must not shrink below
  // it (the centre stack: GapBar / Notices / Timing all at x 0.5). It widens
  // the CONTENT box — extra air lands inside the panel, outside every child's
  // own box — and reports widthSetBy 'min' when it decided.
  //
  // The rules, stated once (argued at length in the page itself):
  //   * a margin GROWS the panel (shrink-to-fit width over every child's ask);
  //   * margins do NOT collapse — the seam between siblings is the SUM;
  //   * every box owns its own column (its content at its own m+b+p);
  //   * gap is junction-only air between stacked children, never at the edges;
  //   * the panel's height is ceiled to a whole cell; the LAST SECTION absorbs
  //     the slack (a button row rides down with it);
  //   * the caption block's advance is ceiled too (titleSlack stretches the
  //     drawn band, bandDrawnBot), so titled and untitled siblings get
  //     identical slack.
  function layoutPanel(spec) {
    const unit = spec.unit;
    const sides = raw => ({ t: raw.t * unit, b: raw.b * unit, l: raw.l, r: raw.r, raw });
    const box = bx => ({ m: sides(bx.margin || { t: 0, r: 0, b: 0, l: 0 }),
                         b: sides(bx.border), p: sides(bx.padding) });
    const Z = { t: 0, r: 0, b: 0, l: 0 };
    const zbox = { m: sides(Z), b: sides(Z), p: sides(Z) };
    const zeroB = bx => ({ m: bx.m, b: sides(Z), p: bx.p });

    let P = box(spec.panel), T = box(spec.title), C = box(spec.content), B = box(spec.button);
    if (!spec.themed) { P = zeroB(P); T = zeroB(T); C = zeroB(C); B = zeroB(B); }

    const hasCaption = !!spec.caption;
    const hasTitle = spec.themed && spec.band && hasCaption;
    const hasCard = spec.themed && spec.card;
    const nBtn = spec.buttons ? spec.buttons.n : 0;
    // A BOX EXISTS WHENEVER ITS CONTENT DOES, but its BORDER exists only when
    // the art filling it is drawn. Collapsing the whole box on hasTitle/hasCard
    // made titleMargin/titlePadding/contentMargin/contentPadding dead on every
    // unthemed panel; spending the border regardless then reserved a slice
    // nobody paints on a themed panel with [card] hud-content = 0. Both
    // switches say the same thing to the border as to the painter. See
    // panel_box.h.
    const t = hasTitle ? (hasCaption ? T : zbox) : zeroB(hasCaption ? T : zbox);
    const c = hasCard ? C : zeroB(C);
    const b = nBtn > 0 ? B : zbox;

    // ---- horizontal: shrink-to-fit over every child, each with its OWN column
    const insetL = x => x.m.l + x.b.l + x.p.l, insetR = x => x.m.r + x.b.r + x.p.r;
    const panelInnerLeft = P.b.l + P.p.l;
    const btnW = nBtn > 0 ? spec.buttons.w + b.b.l + b.p.l + b.p.r + b.b.r : 0;
    // Their facing margins PLUS the junction gap -- a button row is a stack laid
    // sideways, and `gap` names the buttons among the children it separates. See
    // panel_box.h: without it, zeroing buttonMargin makes adjacent labels touch.
    const gapX = spec.gap || 0;
    const btnSeam = b.m.r + b.m.l + gapX;
    const btnRowW = nBtn > 0 ? nBtn * btnW + (nBtn - 1) * btnSeam : 0;
    // THE BODY, normalised into BANDS. `bands` is the general form (a band is a
    // horizontal group of columns, each with its own width ask and its own stack
    // of sections); `sections`/`cols` is the one-column shorthand every panel
    // still states its body in, and builds exactly one band of one column. See
    // panel_box.h and the README's design note.
    const bands = (spec.bands && spec.bands.length ? spec.bands : [
      { columns: [{ cols: spec.cols, sections: spec.sections }] },
    ]).map(bd => ({
      columns: (bd.columns && bd.columns.length ? bd.columns : [{}]).map(col => ({
        cols: col.cols || 0,
        sections: (col.sections && col.sections.length ? col.sections : [0]),
      })),
    }));
    // A BAND'S ASK is its columns' asks plus the seams between them; the rows ask
    // is the widest band. One column collapses to the old expression.
    const colSeam = c.m.r + c.m.l + gapX;
    const rowsAsk = bands.reduce((wide, bd) => {
      const w = bd.columns.reduce((acc, col) => acc + insetL(c) + col.cols + insetR(c), 0)
              + (bd.columns.length - 1) * colSeam;
      return w > wide ? w : wide;
    }, 0);
    const asks = [{ k: 'rows', w: rowsAsk }];
    if (hasCaption) asks.push({ k: 'caption', w: insetL(t) + spec.caption.w + insetR(t) });
    if (nBtn > 0) asks.push({ k: 'buttons', w: insetL(b) + btnRowW + insetR(b) });
    const winner = asks.reduce((a, x) => x.w > a.w + 1e-9 ? x : a, asks[0]);
    let innerW = winner.w;
    let widthSetBy = winner.k;
    if (spec.minPanelW) {
      const chrome = panelInnerLeft + P.p.r + P.b.r;
      if (spec.minPanelW - chrome > innerW + 1e-9) {
        innerW = spec.minPanelW - chrome;
        widthSetBy = 'min';
      }
    }
    const captionX = panelInnerLeft + insetL(t);
    const rowsX = panelInnerLeft + insetL(c);
    const buttonX = panelInnerLeft + insetL(b);
    const panelCols = panelInnerLeft + innerW + P.p.r + P.b.r;
    const columnSplit = hasCaption ? Math.abs(captionX - rowsX) : 0;

    // ---- vertical, outside in ----------------------------------------------
    const gapY = (spec.gap || 0) * unit;
    const panelInner = P.b.t + P.p.t;
    let y = panelInner;
    let titleTop = null, titleH = 0, titleBot = null, titleSlack = 0, bandDrawnBot = null;
    if (hasCaption) {
      titleTop = y + t.m.t;
      titleH = t.b.t + t.p.t + spec.caption.h + t.p.b + t.b.b;
      titleBot = titleTop + titleH;
      // Caption block advance quantized up to a whole cell (same epsilon as
      // panelH): the caption glyph box is the one fractional term differing
      // between titled and untitled panels, so this makes their ceil slack
      // identical. The remainder stretches the DRAWN band bottom
      // (bandDrawnBot — owner absorbs its remainder, like the last section
      // absorbs slackY); caption and terms stay
      // glyph-exact; siblings below move in whole-cell steps. The band→card
      // junction gap is folded INTO the quantized advance — it exists only
      // when the caption does, so outside it would be a second differential
      // fractional term and reintroduce the titled/untitled slack split.
      const adv = t.m.t + titleH + t.m.b + gapY;
      titleSlack = Math.ceil(adv - 1e-9) - adv;
      bandDrawnBot = titleBot + (hasTitle ? titleSlack : 0);
      y += adv + titleSlack;
    }
    // ONE BOX PER SECTION, stacked down its column; the seam between two is the
    // sum of their margins plus the junction gap. A BAND is as tall as its
    // tallest column, and THE LAST COLUMN ABSORBS THE LEFTOVER WIDTH — which is
    // what keeps the one-column case identical to the plain stack it replaced.
    const bandGeoms = [];
    let firstBand = true;
    for (const bd of bands) {
      if (!firstBand) y += gapY;
      firstBand = false;
      const nCol = bd.columns.length;
      let colLeft = panelInnerLeft, bandBot = y;
      const columns = [];
      for (let ci = 0; ci < nCol; ci++) {
        const colAsk = bd.columns[ci];
        const w = ci === nCol - 1 ? (panelInnerLeft + innerW) - colLeft
                                  : insetL(c) + colAsk.cols + insetR(c);
        const col = { left: colLeft, w, sections: [] };
        let cy = y;
        for (const h of colAsk.sections) {
          if (col.sections.length) cy += gapY;
          const top = cy + c.m.t;
          const rowsTop = top + c.b.t + c.p.t;
          const bot = rowsTop + h + c.p.b + c.b.b;
          col.sections.push({ top, bot, rowsTop, h });
          cy = bot + c.m.b;
        }
        if (cy > bandBot) bandBot = cy;
        colLeft += w + colSeam;
        columns.push(col);
      }
      bandGeoms.push({ top: y, bot: bandBot, columns });
      y = bandBot;
    }
    // THE FLATTENED VIEW: first column of every band, top to bottom.
    const sections = [];
    for (const bd of bandGeoms) for (const sec of bd.columns[0].sections) sections.push(sec);
    const seam = sections.length > 1 ? (c.m.b + c.m.t + gapY) : 0;
    // The button row, laid along X. Its own margin above and below, like any sibling.
    let btnTop = null, btnH = 0;
    const btns = [];
    if (nBtn > 0) {
      y += gapY;
      btnTop = y + b.m.t;
      btnH = b.b.t + b.p.t + spec.buttons.h + b.p.b + b.b.b;
      let bx = buttonX - b.b.l - b.p.l;
      for (let k = 0; k < nBtn; k++) {
        btns.push({ x: bx, w: btnW, labelX: bx + b.b.l + b.p.l });
        bx += btnW + btnSeam;
      }
      y = btnTop + btnH + b.m.b;
    }
    const artBot = y + P.p.b + P.b.b;
    const panelH = Math.ceil(artBot - 1e-9);
    const slackY = panelH - artBot;
    // The ceil remainder grows the LAST SECTION's own box, buttons or not
    // (content box = drawn card, one rectangle); a button row rides down with
    // it so its junction and the bottom chrome stay term-exact. Air parked
    // beside the button — bare below it or joined to the junction above —
    // was reported as a bug both times it was tried.
    //
    // THE COLUMN THAT SET THE BAND'S BOTTOM absorbs it, not columns[0]
    // blindly — see panel_box.h for the settings-panel clearance wobble that
    // pinned this. The flattened list shares objects with the bands, so a
    // column-0 absorber updates it automatically and any other column leaves
    // it term-exact, exactly as the C++ copies behave.
    const lastBd = bandGeoms[bandGeoms.length - 1];
    let tallCol = 0;
    let tallBot = lastBd.columns[0].sections.length
        ? lastBd.columns[0].sections[lastBd.columns[0].sections.length - 1].bot : -1e18;
    for (let ci = 1; ci < lastBd.columns.length; ci++) {
      const cs = lastBd.columns[ci].sections;
      if (!cs.length) continue;
      const b2 = cs[cs.length - 1].bot;
      if (b2 > tallBot + 1e-9) { tallBot = b2; tallCol = ci; }
    }
    const absorber = lastBd.columns[tallCol].sections;
    if (absorber.length) {
      absorber[absorber.length - 1].h += slackY;
      absorber[absorber.length - 1].bot += slackY;
    }
    if (nBtn > 0) btnTop += slackY;

    const sq = bd => Math.abs(bd.raw.t - bd.raw.r) < 1e-9 && Math.abs(bd.raw.r - bd.raw.b) < 1e-9
                  && Math.abs(bd.raw.b - bd.raw.l) < 1e-9;
    // A block child SPANS its parent's content box, inset by its own margins.
    const lOf = x => panelInnerLeft + x.m.l;
    const wOf = x => innerW - x.m.l - x.m.r;
    return {
      P, T: t, C: c, B: b, hasTitle, hasCard, nBtn, widthSetBy, columnSplit,
      bands: bandGeoms,
      artBot, panelInner, panelInnerLeft, innerW,
      titleTop, titleH, titleBot, titleSlack, bandDrawnBot,
      sections, seam, btnTop, btnH, btns, btnW,
      titleLeft: lOf(t), titleW: wOf(t), cardLeft: lOf(c), cardW: wOf(c),
      panelH, slackY, panelCols, cols: spec.cols,
      captionX, rowsX, buttonX,
      rowsTop: sections[0].rowsTop, contentH: sections[0].h,
      cardTop: sections[0].top, cardBot: sections[sections.length - 1].bot,
      captionY: hasCaption ? titleTop + t.b.t + t.p.t : 0,
      squareBorders: sq(P.b) && (!hasTitle || sq(T.b)) && (!hasCard || sq(C.b))
                  && (nBtn === 0 || sq(B.b)),
      overTopY: sections[0].rowsTop < P.b.t - 1e-9,
      // (overBotY was here and was dead by construction: panelH is
      // ceil(artBot - 1e-9), so artBot can never exceed it.)
      overX: panelInnerLeft < P.b.l - 1e-9,
    };
  }

  // ---- the page's wrapper --------------------------------------------------
  // Everything TimingHud-shaped lives here: which tier the big row uses, how
  // many rows each section holds, the caption's text width. layoutPanel never
  // sees a font.
  function geom(m, opt) {
    const rowsPer = SECTION_ROWS[opt.sections] || [3];
    const secMeta = rowsPer.map((n, i) => {
      const big = opt.sections > 1 && i === 0;
      const tier = big ? (m.tiers[opt.bigTier] || m.tiers.large) : m.tiers.normal;
      const rowH = big ? (opt.bigRowMode === 'fixed' ? m.row : tier.row) : m.row;
      return { n, big, tier, rowH };
    });
    const hasTitle = opt.themed && opt.band && opt.caption;
    const spec = {
      unit: m.unit,
      panel:   { border: parseSides(opt.panelBorder), padding: parseSides(opt.panelPad) },
      title:   { margin: parseSides(opt.titleMargin), border: parseSides(opt.titleBorder),
                 padding: parseSides(opt.titlePad) },
      content: { margin: parseSides(opt.contentMargin), border: parseSides(opt.contentBorder),
                 padding: parseSides(opt.contentPad) },
      button:  { margin: parseSides(opt.buttonMargin), border: parseSides(opt.buttonBorder),
                 padding: parseSides(opt.buttonPad) },
      themed: opt.themed, band: opt.band, card: opt.card,
      // The caption box is the NORMAL row whatever the tier, band or not —
      // mirrors resolvePanelSpec (one band height across the surface; a
      // Large title's ink fits the 2-cell row). The glyph ink-centres in it.
      caption: opt.caption ? { w: CAPTION_CELLS, h: m.cap } : null,
      sections: secMeta.map(s => s.n * s.rowH),
      cols: opt.cols,
      buttons: opt.buttons > 0 ? { n: opt.buttons, w: BUTTON_CHARS, h: m.row } : null,
      gap: isFinite(Number(opt.gap)) ? Number(opt.gap) : 0,
    };
    const g = layoutPanel(spec);
    // Decorate with the glyph-vs-row fields the page draws for the big row.
    g.sections.forEach((sec, i) => {
      const s = secMeta[i];
      sec.rows = s.n; sec.rowH = s.rowH; sec.big = s.big; sec.tier = s.tier;
      sec.cell = s.tier.size; sec.ink = m.inkH * s.tier.size;
      sec.cellOver = s.tier.size - s.rowH; sec.inkOver = m.inkH * s.tier.size - s.rowH;
    });
    g.inkOverflow = g.sections.reduce((a, sc) => Math.max(a, sc.inkOver), -1e9);
    g.cellOverflow = g.sections.reduce((a, sc) => Math.max(a, sc.cellOver), -1e9);
    return g;
  }

  // Does this distance land on a grid cell? The whole question the overlay exists for.
  const onGrid = v => Math.abs(v - Math.round(v)) < 1e-4;

  const n2 = v => { const r = Math.round(v * 1000) / 1000;
                    return (Math.abs(r) < 0.0005 ? '0' : r.toFixed(3).replace(/0+$/, '').replace(/\.$/, '')); };

  // ---- nine-slice painter --------------------------------------------------
  // THE NINE RECTS, from per-side borders. One function, used by BOTH the
  // texture view and the quad view. The clamp scales opposing borders together
  // when they would exceed the box (NineSlice::clampedBorder does the same
  // thing with one factor for both axes).
  const PARTS3 = [['tl', 'top', 'tr'], ['l', 'mid', 'r'], ['bl', 'bot', 'br']];
  function sliceRects(x, y, w, h, bd) {
    let l = bd.l, r = bd.r, t = bd.t, b = bd.b;
    if (l + r > w && l + r > 0) { const k = w / (l + r); l *= k; r *= k; }
    if (t + b > h && t + b > 0) { const k = h / (t + b); t *= k; b *= k; }
    const xs = [x, x + l, x + w - r, x + w], ys = [y, y + t, y + h - b, y + h];
    const out = [];
    for (let row = 0; row < 3; row++)
      for (let col = 0; col < 3; col++)
        out.push({ l: xs[col], t: ys[row], r: xs[col + 1], b: ys[row + 1],
                   part: PARTS3[row][col] });
    return out;
  }

  // THE FILL COMPLEMENT — NineSlice::cutFill, transcribed. A slab sweep, not a
  // general rectangle boolean; see the C++ header for the shape's rationale.
  function cutFill(centre, covers) {
    const EPS = 1e-9, out = [];
    if (centre.r - centre.l <= EPS || centre.b - centre.t <= EPS) return out;
    const cs = [];
    for (const c0 of covers) {
      const c = { l: Math.max(c0.l, centre.l), r: Math.min(c0.r, centre.r),
                  t: Math.max(c0.t, centre.t), b: Math.min(c0.b, centre.b) };
      if (c.r - c.l > EPS && c.b - c.t > EPS) cs.push(c);
    }
    const xs = [centre.l, centre.r];
    for (const c of cs) { xs.push(c.l); xs.push(c.r); }
    xs.sort((a, b) => a - b);
    for (let i = 0; i + 1 < xs.length; i++) {
      const x0 = xs[i], x1 = xs[i + 1];
      if (x1 - x0 <= EPS) continue;
      const mid = (x0 + x1) / 2;
      const iv = cs.filter(c => c.l <= mid && mid <= c.r)
                   .map(c => [c.t, c.b]).sort((a, b) => a[0] - b[0]);
      let y = centre.t;
      for (const [t0, b0] of iv) {
        if (t0 - y > EPS) out.push({ l: x0, t: y, r: x1, b: t0 });
        if (b0 > y) y = b0;
      }
      if (centre.b - y > EPS) out.push({ l: x0, t: y, r: x1, b: centre.b });
    }
    return out;
  }

  // EVERY QUAD THE PANEL WOULD EMIT, in draw order, in cells. What covers the
  // fill is the plugin's own list and deliberately not a tidier one: the title
  // band and the section cards, and NOT the buttons (finalizeThemedFill).
  //
  // THE FRAME IS panelH TALL, NOT artBot: the plugin draws the background to the
  // CEILED edge (PanelPlan::height() is H(g.panelH)), and the difference is air
  // the last section grows into — a frame at artBot ends above its own last
  // card. Hidden while Records was the only panel either page drew: its artBot
  // lands on a whole cell by luck.
  function collectQuads(g, opt, themed, doCut) {
    const q = [];
    const push = (r, role, part) => q.push({ l: r.l, t: r.t, r: r.r, b: r.b, role,
                                             part: part || r.part });
    if (!themed) {
      push({ l: 0, t: 0, r: g.panelCols, b: g.panelH }, 'panel fill', 'flat');
    } else {
      const fr = sliceRects(0, 0, g.panelCols, g.panelH, g.P.b);
      for (const sl of fr) if (sl.part !== 'mid') push(sl, 'panel border');
      const centre = fr.find(sl => sl.part === 'mid');
      const covers = [];
      if (g.hasTitle) covers.push({ l: g.titleLeft, t: g.titleTop,
                                    r: g.titleLeft + g.titleW, b: g.bandDrawnBot });
      if (g.hasCard) for (const sec of g.sections)
        covers.push({ l: g.cardLeft, t: sec.top, r: g.cardLeft + g.cardW, b: sec.bot });
      if (doCut) for (const st of cutFill(centre, covers)) push(st, 'panel fill', 'strip');
      else push(centre, 'panel fill', 'mid');
    }
    if (g.hasTitle)
      for (const sl of sliceRects(g.titleLeft, g.titleTop, g.titleW,
                                  g.bandDrawnBot - g.titleTop, g.T.b))
        push(sl, 'title');
    if (g.hasCard) g.sections.forEach((sec, i) => {
      for (const sl of sliceRects(g.cardLeft, sec.top, g.cardW, sec.bot - sec.top, g.C.b))
        push(sl, 'section ' + (i + 1));
    });
    if (g.nBtn) g.btns.forEach((bt, k) => {
      for (const sl of sliceRects(bt.x, g.btnTop, bt.w, g.btnH, g.B.b))
        push(sl, 'button ' + (k + 1));
    });
    return q;
  }

  // Overlaps BETWEEN SETS. Within one nine-slice the rects tile by construction,
  // so a same-set overlap would be a bug in sliceRects rather than in the layout
  // -- checked anyway, because that is the cheaper thing to be wrong about.
  function findOverlaps(quads) {
    const EPS = 1e-7, hits = [];
    for (let i = 0; i < quads.length; i++)
      for (let j = i + 1; j < quads.length; j++) {
        const a = quads[i], b = quads[j];
        const w = Math.min(a.r, b.r) - Math.max(a.l, b.l);
        const h = Math.min(a.b, b.b) - Math.max(a.t, b.t);
        if (w > EPS && h > EPS)
          hits.push({ a, b, w, h, area: w * h,
                      l: Math.max(a.l, b.l), t: Math.max(a.t, b.t),
                      r: Math.min(a.r, b.r), bo: Math.min(a.b, b.b),
                      sameSet: a.role === b.role });
      }
    return hits;
  }

  return {
    CHARW, PER_CHAR, PER_ROW, ASPECT, SW, SH,
    SECTION_ROWS, BUTTON_CHARS, CAPTION_CELLS,
    metrics, parseSides, layoutPanel, geom,
    onGrid, n2, sliceRects, cutFill, collectQuads, findOverlaps,
  };
}));
