// ============================================================================
// MXBMRP3 Web Overlay — Session charts: derivation math, SVG rendering & carousel
// Part 09/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

// --- Session Charts (carousel of race-progression line charts) ---
// A horizontal page carousel like Best Sectors, but each page is an inline SVG
// line chart of a whole race: lap chart (position per lap), race trace (cumulative
// time vs a reference pace), gap-to-leader, and pace. All four are derived here
// from the raw per-rider lap series the plugin sends (lastData.laps), a direct port
// of the in-game session_charts_math.h so the overlay reads identically to the HUD.
// Race only; auto-shows once when the leader finishes, and is hotkey-forceable.

// Distinct-hue line palette (a port of the in-game POSITION_PALETTE): high
// separation, no greys/blacks that would vanish as thin lines. Cycled by draw order.
var CHART_PALETTE = [
    "#e6194b", "#3cb44b", "#0082c8", "#f58231", "#911eb4", "#46f0f0",
    "#f032e6", "#d2f53c", "#00a0a0", "#fabebe", "#aa6e28", "#aaffc3",
    "#bebe00", "#dcbeff", "#ffe119", "#5082ff"
];
var CHART_NO_VALID_LAP = Math.pow(2, 40);   // matches SessionChartsMath::kNoValidLap

// ---- Derivation math (port of session_charts_math.h) ----------------------
function cmCumulative(lapMs) {
    var out = [], sum = 0;
    for (var i = 0; i < lapMs.length; i++) { sum += lapMs[i]; out.push(sum); }
    return out;
}
// Validity-aware best-lap-so-far (non-race ranking): an invalid lap doesn't set a
// provisional best, but still occupies an entry so it lines up with cumulative().
function cmBestLapSoFar(lapMs, valid) {
    var out = [], best = CHART_NO_VALID_LAP;
    for (var i = 0; i < lapMs.length; i++) {
        var isValid = (i >= valid.length) || valid[i];
        if (isValid && lapMs[i] < best) best = lapMs[i];
        out.push(best);
    }
    return out;
}
// Per-rider per-lap track position (1 = leader) by ranking every rider present at
// that lap by their ranking value (cumulative time / best-lap). 0 = absent that lap.
function cmPositionsPerLap(ranks, raceNums) {
    var n = ranks.length, positions = [], maxLaps = 0, i, lap;
    for (i = 0; i < n; i++) { positions.push(new Array(ranks[i].length).fill(0)); maxLaps = Math.max(maxLaps, ranks[i].length); }
    for (lap = 0; lap < maxLaps; lap++) {
        var present = [];
        for (i = 0; i < n; i++) if (lap < ranks[i].length) present.push(i);
        present.sort(function (a, b) {
            if (ranks[a][lap] !== ranks[b][lap]) return ranks[a][lap] - ranks[b][lap];
            return raceNums[a] - raceNums[b];
        });
        for (var r = 0; r < present.length; r++) positions[present[r]][lap] = r + 1;
    }
    return positions;
}
// Per-rider per-lap gap to the lap leader (ms), leader pinned to 0.
function cmGapToLeaderPerLap(ranks) {
    var n = ranks.length, gaps = [], maxLaps = 0, i, lap;
    for (i = 0; i < n; i++) { gaps.push(new Array(ranks[i].length).fill(0)); maxLaps = Math.max(maxLaps, ranks[i].length); }
    for (lap = 0; lap < maxLaps; lap++) {
        var leaderCum = 0, have = false;
        for (i = 0; i < n; i++) if (lap < ranks[i].length && (!have || ranks[i][lap] < leaderCum)) { leaderCum = ranks[i][lap]; have = true; }
        if (!have) continue;
        for (i = 0; i < n; i++) if (lap < ranks[i].length) gaps[i][lap] = ranks[i][lap] - leaderCum;
    }
    return gaps;
}
function cmLeaderIndex(cumulatives, raceNums) {
    var best = -1, bestLaps = 0, bestCum = 0;
    for (var i = 0; i < cumulatives.length; i++) {
        var laps = cumulatives[i].length;
        if (laps === 0) continue;
        var cum = cumulatives[i][laps - 1], take;
        if (best < 0) take = true;
        else if (laps !== bestLaps) take = laps > bestLaps;
        else if (cum !== bestCum) take = cum < bestCum;
        else take = raceNums[i] < raceNums[best];
        if (take) { best = i; bestLaps = laps; bestCum = cum; }
    }
    return best;
}
function cmMedian(laps) {
    if (!laps.length) return 0;
    var s = laps.slice().sort(function (a, b) { return a - b; }), mid = s.length >> 1;
    // Even count averages the two middle values with integer floor, matching the
    // C++ medianMs() exactly (its int division truncates, not rounds).
    return (s.length % 2) ? s[mid] : Math.floor((s[mid - 1] + s[mid]) / 2);
}
function cmPercentileSorted(sorted, p) {
    if (!sorted.length) return 0;
    if (sorted.length === 1) return sorted[0];
    if (p <= 0) return sorted[0];
    if (p >= 1) return sorted[sorted.length - 1];
    var pos = p * (sorted.length - 1), lo = Math.floor(pos), hi = Math.min(lo + 1, sorted.length - 1);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (pos - lo);
}
// Robust [lo,hi] via Tukey fences, intersected with the data extent — one blown-out
// rider can't stretch the axis and crush the pack. Equals plain min/max on clean data.
function cmRobustRange(vals) {
    if (!vals.length) return null;
    var s = vals.slice().sort(function (a, b) { return a - b; });
    var dataLo = s[0], dataHi = s[s.length - 1];
    if (s.length < 4) return { lo: dataLo, hi: dataHi };
    var q1 = cmPercentileSorted(s, 0.25), q3 = cmPercentileSorted(s, 0.75), iqr = q3 - q1;
    if (iqr <= 0) return { lo: dataLo, hi: dataHi };
    return { lo: Math.max(dataLo, q1 - 1.5 * iqr), hi: Math.min(dataHi, q3 + 1.5 * iqr) };
}

// ---- Sector resolution (port of the same block in session_charts_math.h) --
// The charts above sample once per completed lap. The plugin also sends each rider's
// per-sector times (laps[].s), including the lap IN PROGRESS, so the same curves can be
// drawn 3-4x denser — which is what turns "the gap grew on lap 7" into "the gap grew in
// sector 2 of lap 7". No new ranking logic is needed: cmPositionsPerLap/cmGapToLeaderPerLap
// rank riders at each ARRAY INDEX and never interpret one as a lap, so a series indexed by
// lap*sectors + sector just works. Mirrored helpers — keep in step with the C++ (pinned by
// tests/fixtures/cpp_js_parity.json, asserted from both sides).
function cmCumulativeBySector(sectorMs, sectorsPerLap) {
    var out = [], sum = 0;
    if (!(sectorsPerLap > 0)) return out;
    for (var i = 0; i < sectorMs.length; i++) {
        // Stop at the first hole rather than skipping it: cumulative time is only
        // meaningful as an unbroken sum from the race start, so summing past a missing
        // sector would under-report every later point and show time the rider never made up.
        if (!(sectorMs[i] > 0)) break;
        sum += sectorMs[i];
        out.push(sum);
    }
    return out;
}
// Laps completed at a sector-resolution index: sector s of lap l ENDS at l+(s+1)/sectors,
// so the final sector of a lap coincides with that lap's per-lap sample.
function cmLapsAtSectorIndex(index, sectorsPerLap) {
    if (!(sectorsPerLap > 0)) return 0;
    return (index + 1) / sectorsPerLap;
}
// Top and bottom ABSOLUTE positions among the drawn riders, at the latest sample any of
// them has a position for — the lap chart's Y axis is order among the SHOWN riders, so its
// labels need the real positions there. The bound is derived from the data, not passed in:
// positions follows the FIELD's resolution, and both renderers open-coded it with a
// lap-count bound that was wrong once sector points existed. {0,0} when there is nothing to
// label. Mirrors latestPositionExtent in session_charts_math.h.
function cmLatestPositionExtent(drawnPositions) {
    var maxPoints = 0, i;
    for (i = 0; i < drawnPositions.length; i++) maxPoints = Math.max(maxPoints, drawnPositions[i].length);
    for (var k = maxPoints - 1; k >= 0; k--) {
        var best = -1, worst = -1;
        for (i = 0; i < drawnPositions.length; i++) {
            var p = drawnPositions[i];
            if (k >= p.length || p[k] <= 0) continue;
            if (best < 0 || p[k] < best) best = p[k];
            if (worst < 0 || p[k] > worst) worst = p[k];
        }
        if (best > 0) return { top: best, bottom: worst };
    }
    return { top: 0, bottom: 0 };
}
// Fractional-lap X mapping shared by both resolutions. Anchoring on the first available
// sample keeps it on the left edge either way, so enabling sector points makes a chart
// denser without sliding it sideways. Per-lap (firstLaps=1, laps=l0+1) reduces to the
// original l0/(maxLap-1).
function cmXFracForLaps(laps, firstLaps, maxLaps) {
    var span = maxLaps - firstLaps;
    if (span <= 0) return 0.5;                 // single sample: centre it
    var t = (laps - firstLaps) / span;
    return t < 0 ? 0 : (t > 1 ? 1 : t);
}
// Trace value at a series index: after 1 1/3 laps the reference rider has used 1 1/3
// reference laps. INTEGER arithmetic on the index, not a float lap count — see
// traceValueAtSector in session_charts_math.h for why (a float/double lap count truncates
// to different milliseconds on the two sides). Per-lap (sectorsPerLap 1) reduces to
// refPace*(l+1) - cum.
function cmTraceValueAtSector(refPaceMs, index, sectorsPerLap, cumulativeMs) {
    if (!(sectorsPerLap > 0)) return -cumulativeMs;
    return Math.trunc(refPaceMs * (index + 1) / sectorsPerLap) - cumulativeMs;
}

// ---- Field assembly + drawn-subset selection -----------------------------
// Build the full-field derived data from lastData.laps (already in classification
// order, oldest-first). Positions/gaps must be computed over the WHOLE field.
function chartsHaveData() {
    var l = lastData && lastData.laps;
    if (!l || !l.length) return false;
    for (var i = 0; i < l.length; i++) if (l[i].t && l[i].t.length) return true;
    return false;
}
function buildChartField(isRace) {
    var laps = (lastData && lastData.laps) ? lastData.laps : [];
    var field = { raceNums: [], lapMs: [], lapValid: [], sectorMs: [], cumulative: [], positions: [], gaps: [],
                  refPaceMs: 0, maxLap: 0, isRace: isRace, pointsPerLap: 1, maxLaps: 0 };
    for (var i = 0; i < laps.length; i++) {
        var t = laps[i].t || [];
        if (!t.length) continue;
        var v = laps[i].v || [];
        var valid = [];
        for (var j = 0; j < t.length; j++) valid.push(j < v.length ? !!v[j] : true);
        field.raceNums.push(laps[i].num);
        field.lapMs.push(t);
        field.lapValid.push(valid);
        field.sectorMs.push(laps[i].s || []);
        field.maxLap = Math.max(field.maxLap, t.length);
    }
    var n = field.raceNums.length;

    // Per-lap cumulative is always built: it is the trace baseline's basis (below) and the
    // fallback series, whatever resolution the charts end up drawn at.
    var perLapCum = [];
    for (i = 0; i < n; i++) perLapCum.push(cmCumulative(field.lapMs[i]));

    // Sector resolution, if the client asked for it AND the data supports it. Races only,
    // for the two reasons documented at SessionChartsHud::collectField: off-race the charts
    // rank by best lap so far (which a partial lap cannot improve, so sector points would
    // draw identical stacked values per lap), AND cmBestLapSoFar returns a PER-LAP array —
    // pairing that with a sector-resolution xForPoint would make the Y series and the X
    // mapping disagree about what an index means. Mirrors SessionChartsHud::collectField.
    var sectorsPerLap = chartSectorsPerLap();
    var wantSectors = !!CONFIG.chartSectorPoints && isRace && sectorsPerLap > 0;
    var sectorsUsable = wantSectors, sectorCum = [];
    if (wantSectors) {
        for (i = 0; i < n && sectorsUsable; i++) {
            sectorCum.push(cmCumulativeBySector(field.sectorMs[i], sectorsPerLap));
            // A series that doesn't reach the rider's completed-lap count has a hole in it
            // (a track with misplaced split markers reports a zero sector). Fall the WHOLE
            // field back to per-lap rather than mixing resolutions — the ranking functions
            // compare riders by array index, so mixing would rank one rider's sector
            // against another's lap.
            if (sectorCum[i].length < field.lapMs[i].length * sectorsPerLap) sectorsUsable = false;
        }
    }

    field.pointsPerLap = sectorsUsable ? sectorsPerLap : 1;
    for (i = 0; i < n; i++) field.cumulative.push(sectorsUsable ? sectorCum[i] : perLapCum[i]);

    // X domain end, in laps completed. At sector resolution the furthest-along rider is
    // usually part way through a lap; clamping that to maxLap would stack their last points
    // on the right edge.
    field.maxLaps = field.maxLap;
    if (sectorsUsable) {
        for (i = 0; i < n; i++) {
            if (!field.cumulative[i].length) continue;
            field.maxLaps = Math.max(field.maxLaps,
                cmLapsAtSectorIndex(field.cumulative[i].length - 1, field.pointsPerLap));
        }
    }

    var ranks = [];
    for (i = 0; i < n; i++) ranks.push(isRace ? field.cumulative[i] : cmBestLapSoFar(field.lapMs[i], field.lapValid[i]));
    field.positions = cmPositionsPerLap(ranks, field.raceNums);
    field.gaps = cmGapToLeaderPerLap(ranks);
    if (isRace) {
        // Deliberately from the PER-LAP cumulative: the reference is the leader's average
        // LAP time, so counting sectors as laps would divide it by the sector count and put
        // everyone wildly "ahead" of the reference.
        var li = cmLeaderIndex(perLapCum, field.raceNums);
        if (li >= 0 && perLapCum[li].length) {
            var lc = perLapCum[li];
            field.refPaceMs = Math.floor(lc[lc.length - 1] / lc.length);
        }
    }
    return field;
}
// Stride of every laps[].s array, straight from the plugin (snapshot.sectorCount) — MX
// Bikes/KRP send 3, GP Bikes 4, and the overlay is served by all of them.
//
// This is NOT inferred from the data, and an earlier version that tried to was wrong in the
// common case. `s` also carries the lap in progress, so a rider sits at 3L+k entries against
// L completed laps, and floor((3L+k)/L) = 3 + floor(k/L) — which is 4 for the whole of lap 2
// of every race (L=1, one split crossed) and for any rider lapped far enough that k >= L. A
// wrong stride makes the hole check below fail for everyone, silently dropping the field to
// per-lap, and it flickers frame to frame as riders cross splits.
function chartSectorsPerLap() {
    var n = (lastData && lastData.sectorCount) | 0;
    return (n >= 3 && n <= 4) ? n : 0;
}
// Laps completed at a series index, and at the earliest sample a series can hold.
function chartLapsAt(field, index) { return cmLapsAtSectorIndex(index, field.pointsPerLap); }
function chartFirstLaps(field) { return chartLapsAt(field, 0); }
// Draw the top of the field (leaders — the field is in classification order),
// capped to the shared Panel Rows knob and the line palette size. Colour by draw
// order; the director subject (if on camera and shown) gets a thicker line to stand out.
function selectChartDrawn(field) {
    var rows = Math.min(CONFIG.slotRows, field.raceNums.length, CHART_PALETTE.length);
    var subject = (lastData && lastData.director && lastData.director.active) ? lastData.director.subject : -1;
    var drawn = [];
    for (var i = 0; i < rows; i++) {
        drawn.push({ idx: i, color: CHART_PALETTE[i % CHART_PALETTE.length], num: field.raceNums[i],
                     subject: field.raceNums[i] === subject });
    }
    return drawn;
}

// ---- SVG rendering -------------------------------------------------------
// One <svg> string per chart, drawn in pixel space (viewBox = the page's measured
// px size) so text stays legible and non-scaling strokes stay crisp.
var CHART_TYPES = { LAP: "lap", TRACE: "trace", GAP: "gap", PACE: "pace" };
function chartName(type, isRace) {
    switch (type) {
        case CHART_TYPES.LAP:   return "Lap Chart";
        case CHART_TYPES.TRACE: return "Race Trace";
        case CHART_TYPES.GAP:   return isRace ? "Gap to Leader" : "Gap to Best Lap";
        case CHART_TYPES.PACE:  return "Pace";
    }
    return "";
}
// Compact seconds label, e.g. "12.3s" (or "1:23.4" for >=60s). Rounds to tenths
// BEFORE splitting into minutes so 59.96s can't print "0:60.0". Mirrors the C++
// formatSecs() in hud/session_charts_math.h — keep them in step (same integer rounding).
function fmtChartSecs(ms, showSign) {
    var sign = showSign ? (ms > 0 ? "+" : (ms < 0 ? "-" : "")) : "";
    var tenths = Math.round(Math.abs(ms) / 100);
    var whole = Math.floor(tenths / 10), frac = tenths % 10;
    if (whole >= 60) {
        var secs = whole % 60;
        return sign + Math.floor(whole / 60) + ":" + (secs < 10 ? "0" : "") + secs + "." + frac;
    }
    return sign + whole + "." + frac + "s";
}
function esc(s) { return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); }
// Emit a rider's polyline as one <polyline> per contiguous run (a run breaks where
// pts[i] is null — an absent lap). Thicker for the on-camera subject.
function svgPolyline(pts, color, subject) {
    var w = subject ? 3.0 : 1.8, out = "", run = [];
    function flush() {
        if (run.length >= 2) out += '<polyline class="chart-line" stroke="' + color + '" stroke-width="' + w + '" points="' + run.join(" ") + '"/>';
        run = [];
    }
    for (var i = 0; i < pts.length; i++) {
        if (!pts[i]) { flush(); continue; }
        run.push(pts[i][0].toFixed(1) + "," + pts[i][1].toFixed(1));
    }
    flush();
    return out;
}
function svgTag(x, y, num, color, tagFont) {
    return '<text class="chart-tag" x="' + (x + tagFont * 0.35).toFixed(1) + '" y="' + (y + tagFont * 0.35).toFixed(1) +
           '" font-size="' + tagFont + '" fill="' + color + '">#' + num + '</text>';
}
// Render one chart into an SVG string sized WxH (px). Returns "" for the race-only
// trace in a non-race session (the caller shows a note instead).
function renderChartSvg(type, field, drawn, W, H, rh) {
    var axisFont = Math.max(9, Math.round(rh * 0.42));
    var tagFont = Math.max(10, Math.round(rh * 0.5));
    var padL = Math.round(axisFont * 3.4), padR = Math.round(tagFont * 2.8);
    var padT = Math.round(H * 0.06) + 3, padB = Math.round(axisFont * 1.7);
    var px = padL, py = padT, pw = Math.max(1, W - padL - padR), ph = Math.max(1, H - padT - padB);
    var maxLap = field.maxLap;
    // Per-lap index (the pace chart, and lap-number axis labels), expressed through the
    // shared fractional mapping — it reduces to the original l0/(maxLap-1).
    function xForLap(l0) { return px + cmXFracForLaps(l0 + 1, 1, maxLap) * pw; }
    // Index into the resolution-aware series (cumulative/positions/gaps). Identical to
    // xForLap when the field is per-lap.
    function xForPoint(i) { return px + cmXFracForLaps(chartLapsAt(field, i), chartFirstLaps(field), field.maxLaps) * pw; }
    var svg = ['<svg class="charts-svg" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none">'];
    var gridTop = '<line class="chart-grid" x1="' + px + '" y1="' + py + '" x2="' + (px + pw) + '" y2="' + py + '"/>';
    var gridBot = '<line class="chart-grid" x1="' + px + '" y1="' + (py + ph) + '" x2="' + (px + pw) + '" y2="' + (py + ph) + '"/>';

    // Bottom lap labels + a Y-label pair, shared by all charts.
    // domainEndLaps MUST be the domain the caller placed its points over — the right
    // label is ceil() of it. The pace chart plots per-LAP while the others span
    // field.maxLaps, so passing the wrong one puts "L4" under a chart ending at lap 3.
    function axisLabels(topLabel, botLabel, domainEndLaps) {
        var s = "";
        if (topLabel != null) s += '<text class="chart-axis" text-anchor="end" x="' + (px - 4) + '" y="' + (py + axisFont * 0.9) + '" font-size="' + axisFont + '">' + esc(topLabel) + '</text>';
        if (botLabel != null) s += '<text class="chart-axis" text-anchor="end" x="' + (px - 4) + '" y="' + (py + ph) + '" font-size="' + axisFont + '">' + esc(botLabel) + '</text>';
        s += '<text class="chart-axis" x="' + px + '" y="' + (py + ph + axisFont * 1.2) + '" font-size="' + axisFont + '">L1</text>';
        // The lap the RIGHT EDGE falls in — see addChartAxisLabels in session_charts_hud.cpp.
        // ceil(field.maxLaps), not maxLap: at sector resolution the domain ends at the
        // furthest-along rider's real progress (3.33 laps), which "L3" under-labels. No-op
        // at per-lap resolution.
        var edgeLap = Math.ceil(domainEndLaps);
        if (edgeLap > 1) s += '<text class="chart-axis" text-anchor="end" x="' + (px + pw) + '" y="' + (py + ph + axisFont * 1.2) + '" font-size="' + axisFont + '">L' + edgeLap + '</text>';
        return s;
    }

    if (type === CHART_TYPES.LAP) {
        var K = drawn.length, yRows = Math.max(2, K);
        function yForRow(row0) { return py + (row0 / (yRows - 1)) * ph; }
        // Bump chart: rank the shown riders among themselves at each lap (own row each).
        var rowOf = [];
        for (var di = 0; di < K; di++) rowOf.push(new Array(field.positions[drawn[di].idx].length).fill(-1));
        var maxPoints = 0;
        for (var mi = 0; mi < rowOf.length; mi++) maxPoints = Math.max(maxPoints, rowOf[mi].length);
        for (var lap = 0; lap < maxPoints; lap++) {
            var present = [];
            for (di = 0; di < K; di++) {
                var pos = field.positions[drawn[di].idx];
                if (lap < pos.length && pos[lap] > 0) present.push([pos[lap], di]);
            }
            present.sort(function (a, b) { return a[0] - b[0]; });
            for (var rr = 0; rr < present.length; rr++) rowOf[present[rr][1]][lap] = rr;
        }
        svg.push(gridTop, gridBot);
        var tags = "";
        for (di = 0; di < K; di++) {
            var pts = [], last = null;
            for (lap = 0; lap < rowOf[di].length; lap++) {
                if (rowOf[di][lap] < 0) { pts.push(null); continue; }
                var p = [xForPoint(lap), yForRow(rowOf[di][lap])]; pts.push(p); last = p;
            }
            svg.push(svgPolyline(pts, drawn[di].color, drawn[di].subject));
            if (last) tags += svgTag(last[0], last[1], drawn[di].num, drawn[di].color, tagFont);
        }
        svg.push(tags);
        // Y labels: positions of whoever occupies the top and bottom rows at the latest
        // sample. Shared helper (mirrors latestPositionExtent in session_charts_math.h, and
        // pinned by cpp_js_parity.json) — the search bound must follow the SERIES, not the
        // lap count, which is what the open-coded loop here got wrong.
        var drawnPositions = [];
        for (di = 0; di < K; di++) drawnPositions.push(field.positions[drawn[di].idx]);
        var extent = cmLatestPositionExtent(drawnPositions);
        var topPos = extent.top, botPos = extent.bottom;
        svg.push(axisLabels(topPos > 0 ? "P" + topPos : null, topPos > 0 ? "P" + botPos : null, field.maxLaps));
    } else if (type === CHART_TYPES.TRACE) {
        var hasData = field.refPaceMs > 0;
        var vals = [];
        for (di = 0; di < drawn.length; di++) {
            var cum = field.cumulative[drawn[di].idx];
            // Same expression the points below are plotted with — a range scanned at a
            // different resolution than the line would clip the line to the wrong extent.
            for (var l = 0; l < cum.length; l++)
                vals.push(cmTraceValueAtSector(field.refPaceMs, l, field.pointsPerLap, cum[l]));
        }
        var rr2 = cmRobustRange(vals);
        var vMin = Math.min(rr2 ? rr2.lo : 0, 0), vMax = Math.max(rr2 ? rr2.hi : 0, 0);
        if (vMax - vMin < 1000) { vMax += 500; vMin -= 500; }
        var span = vMax - vMin;
        function yForVal(v) { var vc = Math.max(vMin, Math.min(vMax, v)); return py + (vMax - vc) / span * ph; }
        svg.push(gridTop, gridBot);
        if (vMin <= 0 && vMax >= 0) svg.push('<line class="chart-zero" x1="' + px + '" y1="' + yForVal(0).toFixed(1) + '" x2="' + (px + pw) + '" y2="' + yForVal(0).toFixed(1) + '"/>');
        var traceTags = "";
        for (di = 0; di < drawn.length; di++) {
            var cumr = field.cumulative[drawn[di].idx], tpts = [], tlast = null;
            for (l = 0; l < cumr.length; l++) {
                var tv = [xForPoint(l), yForVal(cmTraceValueAtSector(field.refPaceMs, l, field.pointsPerLap, cumr[l]))];
                tpts.push(tv); tlast = tv;
            }
            svg.push(svgPolyline(tpts, drawn[di].color, drawn[di].subject));
            if (tlast) traceTags += svgTag(tlast[0], tlast[1], drawn[di].num, drawn[di].color, tagFont);
        }
        svg.push(traceTags);
        svg.push(axisLabels(hasData ? fmtChartSecs(vMax, true) : null, hasData ? fmtChartSecs(vMin, true) : null, field.maxLaps));
    } else if (type === CHART_TYPES.GAP) {
        var gvals = [];
        for (di = 0; di < drawn.length; di++)
            for (var gi = 0; gi < field.gaps[drawn[di].idx].length; gi++) { var g = field.gaps[drawn[di].idx][gi]; if (g < CHART_NO_VALID_LAP / 2) gvals.push(g); }
        var grr = cmRobustRange(gvals);
        var gapMax = Math.max(1000, grr ? grr.hi : 0);
        function yForGap(g) { var gc = Math.max(0, Math.min(gapMax, g)); return py + gc / gapMax * ph; }
        svg.push(gridTop, gridBot);
        var gapTags = "";
        for (di = 0; di < drawn.length; di++) {
            var gaps = field.gaps[drawn[di].idx], gpts = [], glast = null;
            for (l = 0; l < gaps.length; l++) { var gv = [xForPoint(l), yForGap(gaps[l])]; gpts.push(gv); glast = gv; }
            svg.push(svgPolyline(gpts, drawn[di].color, drawn[di].subject));
            if (glast) gapTags += svgTag(glast[0], glast[1], drawn[di].num, drawn[di].color, tagFont);
        }
        svg.push(gapTags);
        svg.push(axisLabels("0.0s", fmtChartSecs(gapMax, false), field.maxLaps));
    } else if (type === CHART_TYPES.PACE) {
        // Clean racing-pace band: exclude the opening lap, invalid laps, and laps
        // slower than median*1.4 (matches the in-game ELEM_FILTER_OUTLIERS default).
        var factor = 1.4;
        function paceValid(di2, l2) { var v = field.lapValid[drawn[di2].idx]; return l2 >= v.length || v[l2]; }
        var clean = [];
        for (di = 0; di < drawn.length; di++) {
            var lm = field.lapMs[drawn[di].idx];
            for (l = 0; l < lm.length; l++) if (paceValid(di, l) && l !== 0) clean.push(lm[l]);
        }
        var median = cmMedian(clean);
        function included(di2, l2, ms) { if (!paceValid(di2, l2)) return false; if (l2 <= 0) return false; if (median <= 0) return true; return ms <= median * factor; }
        var pMin = -1, pMax = -1;
        for (di = 0; di < drawn.length; di++) {
            var lmr = field.lapMs[drawn[di].idx];
            for (l = 0; l < lmr.length; l++) { if (!included(di, l, lmr[l])) continue; if (pMin < 0 || lmr[l] < pMin) pMin = lmr[l]; if (pMax < 0 || lmr[l] > pMax) pMax = lmr[l]; }
        }
        var hasPace = pMin >= 0 && pMax >= 0;
        if (!hasPace) { pMin = 0; pMax = 1; }
        if (pMax - pMin < 500) { pMax += 250; pMin = Math.max(0, pMin - 250); }
        var pspan = Math.max(1, pMax - pMin);
        function yForPace(v) { return py + (pMax - v) / pspan * ph; }
        svg.push(gridTop, '<line class="chart-grid" x1="' + px + '" y1="' + (py + ph / 2) + '" x2="' + (px + pw) + '" y2="' + (py + ph / 2) + '"/>', gridBot);
        var paceTags = "";
        for (di = 0; di < drawn.length; di++) {
            var lmp = field.lapMs[drawn[di].idx], ppts = [], plast = null;
            for (l = 0; l < lmp.length; l++) {
                if (!included(di, l, lmp[l])) { ppts.push(null); continue; }
                var pv = [xForLap(l), yForPace(lmp[l])]; ppts.push(pv); plast = pv;
            }
            svg.push(svgPolyline(ppts, drawn[di].color, drawn[di].subject));
            if (plast) paceTags += svgTag(plast[0], plast[1], drawn[di].num, drawn[di].color, tagFont);
        }
        svg.push(paceTags);
        // maxLap, not maxLaps: the pace chart plots lap TIMES through xForLap, so its
        // domain is whole laps whatever resolution the field is at.
        svg.push(axisLabels(hasPace ? fmtChartSecs(pMax, false) : null, hasPace ? fmtChartSecs(pMin, false) : null, maxLap));
    }
    svg.push('</svg>');
    return svg.join("");
}

// ---- Carousel wiring (mirrors the best-sectors carousel) ------------------
var chartsPageTimer = null, chartsPageIndex = 0, chartsPageCount = 0;
var chartsPageSize = 0;
var chartsEmptyShown = false;   // true while the forced "No data" placeholder is up

// Enabled chart types for the session, in fixed order. Race Trace is race-only.
function enabledChartTypes(isRace) {
    var out = [];
    if (CONFIG.chartLap)   out.push(CHART_TYPES.LAP);
    if (CONFIG.chartTrace && isRace) out.push(CHART_TYPES.TRACE);
    if (CONFIG.chartGap)   out.push(CHART_TYPES.GAP);
    if (CONFIG.chartPace)  out.push(CHART_TYPES.PACE);
    return out;
}

// Build one carousel page (SVG) per enabled chart. Returns the page count (0 aborts).
function buildChartsPages() {
    var session = lastData && lastData.session;
    var isRace = !!(session && session.isRace);
    var types = enabledChartTypes(isRace);
    if (!types.length || !chartsHaveData()) return 0;
    var field = buildChartField(isRace);
    var drawn = selectChartDrawn(field);

    var rh = measureRowHeight();
    chartsPageSize = Math.min(CONFIG.slotRows, Math.max(1, visibleTowerRows(lastData.standings) - 1));
    var H = chartsPageSize * rh;
    // The panel is still display:none at build() time, so measure the always-visible
    // tower (standings-body) — the panel spans the same width (left:0/right:0). Using
    // the real px width as the viewBox width keeps the SVG 1:1 (no glyph distortion
    // under preserveAspectRatio="none").
    var W = standingsBody.clientWidth || chartsViewport.clientWidth || 320;
    chartsViewport.style.height = ((chartsPageSize + 1) * rh) + "px";   // title row + chart
    chartsTrack.textContent = "";
    for (var p = 0; p < types.length; p++) {
        chartsTrack.appendChild(makeChartPage(
            carouselTitleRow("charts-title", chartName(types[p], isRace), rh),
            renderChartSvg(types[p], field, drawn, W, H, rh), H));
    }
    return types.length;
}

// Assemble a chart page: the title row (slides in with the page) atop the SVG,
// whose height is pinned to H px so the viewBox stays 1:1 (no glyph distortion).
function makeChartPage(titleEl, svgHtml, H) {
    var page = document.createElement("div");
    page.className = "charts-page";
    page.appendChild(titleEl);
    var tmp = document.createElement("div");
    tmp.innerHTML = svgHtml;
    var svg = tmp.firstChild;
    svg.style.height = H + "px";
    page.appendChild(svg);
    return page;
}

// "No data" placeholder page for a forced panel before any laps exist. One carousel
// page = the title + the SHARED "No data" row (slotEmptyRow, from overlay-panels.js),
// so it reads identically to every other forced-empty bottom-slot panel — same
// wording, same normal font (not the old SVG note in the display font).
function buildChartsEmpty() {
    var rh = measureRowHeight();
    chartsPageSize = 1;
    chartsViewport.style.height = (2 * rh) + "px";   // title + one "No data" row
    chartsTrack.textContent = "";
    chartsPageCount = 1; chartsPageIndex = 0;
    chartsTrack.style.transition = "none"; chartsTrack.style.transform = "translateX(0)";
    void chartsTrack.offsetWidth; chartsTrack.style.transition = "";
    var page = document.createElement("div");
    page.className = "charts-page";
    page.appendChild(carouselTitleRow("charts-title", "Session Charts", rh));
    page.appendChild(slotEmptyRow(rh));
    chartsTrack.appendChild(page);
    // The placeholder self-terminates like the paging carousel would: since there's
    // no data to page through, bound it with a finite self-hide (Panel Time) so a
    // force before any laps can't park it on screen forever. If lap data arrives
    // first, the panel's refresh() upgrades it in place (cancelling this).
    chartsEmptyShown = true;
    if (chartsPageTimer) clearTimeout(chartsPageTimer);
    chartsPageTimer = setTimeout(function () { chartsCtrl.hide(true); },
        Math.max(3000, CONFIG.slotDuration * 1000));
    return 2;   // title + placeholder
}

function scheduleChartsStep() {
    if (chartsPageTimer) clearTimeout(chartsPageTimer);
    chartsPageTimer = setTimeout(chartsStep, slotPageMs(chartsPageCount));
}
function chartsStep() {
    if (chartsPageIndex + 1 < chartsPageCount) {
        chartsPageIndex++;
        chartsTrack.style.transform = "translateX(-" + (chartsPageIndex * 100) + "%)";
        scheduleChartsStep();
    } else {
        chartsCtrl.hide(true);   // paged through every chart — natural end, arm rest
    }
}

// Auto-show trigger: fires once when the race leader (P1) finishes. Returns a
// non-null signature only once the leader is finished; null before that (so the
// event-driven show seeds to null and fires on the not-finished -> finished edge).
// If the leader is already finished on connect, the signature seeds non-null and
// does not replay.
function chartsLeaderFinishKey() {
    var st = lastData && lastData.standings;
    if (!st || !st.length) return null;
    var leader = null;
    for (var i = 0; i < st.length; i++) if (st[i].pos === 1) { leader = st[i]; break; }
    if (!leader) return null;
    return leader.finished ? ("charts-fin:" + leader.num) : null;
}

var chartsCtrl = createSlotPanel({
    panel: chartsPanel, name: "charts",
    // Higher priority than the timed boards (1) so the leader-finish summary
    // reliably pre-empts whatever board happens to hold the slot at the flag,
    // rather than losing its one-shot event to a busy slot. It only ever takes
    // the slot on that finish event or a manual force, so it never hogs it.
    priority: 2,
    enabled: function () { return CONFIG.charts; },
    // Race sessions only (the charts are race-progression views), with lap data and
    // at least one enabled chart page. A caster can force it outside this via hotkey.
    eligible: function (s) { return !!(s && s.isRace && chartsHaveData() && enabledChartTypes(true).length > 0); },
    eventKey: chartsLeaderFinishKey,   // auto-show once when P1 finishes
    // The leader-finish trigger is one-shot and the finish signature is stable, so
    // if a broadcaster-forced panel (or another equal/higher-priority panel) holds
    // the slot at the flag, defer the show instead of consuming the event — charts
    // then slides in the moment the slot frees rather than losing its summary. It
    // still never OVERRIDES a manual force (priority stays below Infinity).
    deferEventWhenBlocked: true,
    build: function () {
        var pages = buildChartsPages();
        if (!pages) return 0;
        chartsPageCount = pages;
        chartsPageIndex = 0;
        chartsTrack.style.transition = "none";
        chartsTrack.style.transform = "translateX(0)";
        void chartsTrack.offsetWidth;
        chartsTrack.style.transition = "";
        chartsEmptyShown = false;
        return chartsPageSize + 1;   // title + chart rows
    },
    autoHide: function () { return 0; },   // self-terminating via paging
    showEmptyWhenForced: true,             // forced before any laps: show a "No data" chart
    renderEmpty: function () { return buildChartsEmpty(); },
    // Placeholder-aware (like the lap boards): when a forced "No data"
    // placeholder is up and lap data arrives, upgrade it in place into the real
    // carousel and start paging — cancelling the placeholder's self-hide. A no-op
    // during normal paging (chartsEmptyShown is false then).
    refresh: function () {
        if (!chartsEmptyShown || !chartsHaveData()) return;
        var pages = buildChartsPages();
        if (!pages) return;   // still nothing to show (e.g. all chart types disabled)
        chartsEmptyShown = false;
        chartsPageCount = pages;
        chartsPageIndex = 0;
        chartsTrack.style.transition = "none";
        chartsTrack.style.transform = "translateX(0)";
        void chartsTrack.offsetWidth;
        chartsTrack.style.transition = "";
        chartsCtrl.resize(chartsPageSize + 1);
        scheduleChartsStep();   // start paging (replaces the placeholder's self-hide timer)
    },
    onShow: function () { scheduleChartsStep(); },
    onHide: function () { chartsEmptyShown = false; if (chartsPageTimer) { clearTimeout(chartsPageTimer); chartsPageTimer = null; } }
});

