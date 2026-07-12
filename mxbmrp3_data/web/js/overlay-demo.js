// ============================================================================
// MXBMRP3 Web Overlay — Demo mode (?demo) & app initialization
// Part 11/11 of the overlay client (split from the former monolithic app.js).
// Ordered classic script: files share one global scope and MUST load in the
// order listed in index.html. Customize freely — served from disk by the plugin.
// ============================================================================
"use strict";

// race, looping. Nothing here touches the live code path.
function startDemo() {
    demoActive = true;                 // suppress persistence of scaled timings
    // Demo/test hook: expose the bottom-slot force so a preview page or a Playwright
    // test can bring a panel up deterministically (e.g. the session charts) without
    // waiting for its natural trigger. Demo-only — never assigned on the live path.
    window.mxbmrp3ForceSlot = forceSlot;
    appendStatusLine("Demo mode — replaying a sample warmup + race", "ok");
    versionAnnounced = true;           // suppress the "Connected" banner
    overlay.classList.remove("disconnected");

    // Compress the bottom-slot timing for the demo by scaling each panel's on-screen
    // duration so the preview is snappy. The lap / sector boards are event-driven -
    // they trigger off the demo's synthetic new laps / sectors, so there's no cadence
    // left to scale, only the dwell time.
    // In-memory only — not persisted to the user's real settings.
    var DEMO_PANEL_SCALE = 0.4;
    CONFIG.slotDuration   *= DEMO_PANEL_SCALE;   // boards, carousels (per-page derived)
    CONFIG.slotRest       *= DEMO_PANEL_SCALE;   // inter-panel rest, kept proportional

    var TICK_MS = 250;                 // render cadence (real ms)
    // Base sim speed (virtual ms elapsed per real ms), scaled by an optional
    // ?speed=<mult> (or #speed=) so you can race through the synthetic session
    // — or slow it down — while previewing: 1 = default, 2 = twice as fast,
    // 0.5 = half. Only the simulation clock scales; the real-time render
    // cadence, panel dwell and focus-card rotation are unchanged. Clamped to a
    // sane range; a missing/garbage value falls back to the default.
    var speedMatch = (location.search + location.hash).match(/[?&#]speed=([0-9]*\.?[0-9]+)/i);
    var SPEED_MULT = speedMatch ? clamp(parseFloat(speedMatch[1]), 0.1, 50) : 1;
    var SPEED = 8 * SPEED_MULT;
    if (SPEED_MULT !== 1) appendStatusLine("Demo speed ×" + SPEED_MULT, "ok");
    // Optional phase lock: ?race jumps straight into the race (skips the warmup) and
    // loops back to the race when it finishes; ?warmup stays in the warmup and loops it.
    // Lets a test/preview go straight to the phase it cares about instead of sitting
    // through the other. Default (neither) is the full warmup -> race -> loop cycle.
    var urlStr = location.search + location.hash;
    var FORCE_RACE   = /[?&#]race\b/i.test(urlStr);
    var FORCE_WARMUP = /[?&#]warmup\b/i.test(urlStr) && !FORCE_RACE;
    // ?charts previews the session-charts carousel: once the race is up and laps
    // exist, keep forcing the panel so it's always on screen to eyeball/screenshot
    // (it self-terminates through its pages, then re-forces). Parallel to ?warmup/?race.
    var FORCE_CHARTS = /[?&#]charts\b/i.test(urlStr);
    if (FORCE_RACE)   appendStatusLine("Demo phase: race (warmup skipped)", "ok");
    if (FORCE_WARMUP) appendStatusLine("Demo phase: warmup", "ok");
    if (FORCE_CHARTS) appendStatusLine("Demo: forcing session-charts panel", "ok");
    var nextChartsForce = 0;           // wall-clock gate for the ?charts preview re-force
    var WARMUP_LEN = 8 * 60000;        // 8-minute warmup, then the race (virtual)
    var SESSION_LEN = 12 * 60000;      // 12-minute timed race (virtual)

    // Give one rider a tracked-rider plate (the #1 points leader) so the demo
    // shows the red/white number badge without configuring tracked riders.
    var DEMO_TRACKED_NUM = 1;
    var DEMO_TRACKED_PLATE = "#e10600";   // red plate -> white number (luma < 128)

    // Roster — names/brands evocative of a 450 main event. `pace` is the
    // rider's baseline lap time in ms; the leader has the lowest.
    var roster = [
        { num: 94,  name: "K. Roczen",    bike: "Suzuki RM-Z450",   brand: "Suzuki",     color: "#f4d300", pace: 45300 },
        { num: 96,  name: "H. Lawrence",  bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 45500 },
        { num: 32,  name: "J. Cooper",    bike: "Yamaha YZ450F",    brand: "Yamaha",     color: "#1b6ec2", pace: 46400 },
        { num: 27,  name: "M. Stewart",   bike: "Husqvarna FC450",  brand: "Husqvarna",  color: "#c8c8c8", pace: 46600 },
        { num: 26,  name: "J. Prado",     bike: "KTM 450 SX-F",     brand: "KTM",        color: "#ff6600", pace: 46900 },
        { num: 1,   name: "C. Webb",      bike: "Yamaha YZ450F",    brand: "Yamaha",     color: "#1b6ec2", pace: 47200 },
        { num: 17,  name: "J. Savatgy",   bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 47800 },
        { num: 14,  name: "D. Ferrandis", bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 48100 },
        { num: 28,  name: "C. Craig",     bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 48400 },
        { num: 15,  name: "D. Wilson",    bike: "GasGas MC450F",    brand: "GasGas",     color: "#d4002a", pace: 48700 },
        { num: 719, name: "V. Friese",    bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 49100 },
        { num: 51,  name: "J. Barcia",    bike: "GasGas MC450F",    brand: "GasGas",     color: "#d4002a", pace: 49300 },
        { num: 21,  name: "J. Anderson",  bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 49500 },
        { num: 7,   name: "A. Plessinger", bike: "KTM 450 SX-F",    brand: "KTM",        color: "#ff6600", pace: 49800 },
        { num: 36,  name: "B. Sexton",    bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 50000 },
        { num: 24,  name: "R. Brown",     bike: "Yamaha YZ450F",    brand: "Yamaha",     color: "#1b6ec2", pace: 50300 },
        { num: 45,  name: "C. Sexton",    bike: "Honda CRF450",     brand: "Honda",      color: "#e2231a", pace: 50500 },
        { num: 12,  name: "S. Cianciarulo", bike: "Kawasaki KX450", brand: "Kawasaki",   color: "#4caf00", pace: 50800 },
        { num: 38,  name: "H. Deegan",    bike: "Kawasaki KX450",   brand: "Kawasaki",   color: "#4caf00", pace: 51000 },
        { num: 75,  name: "B. Hampshire", bike: "KTM 450 SX-F",     brand: "KTM",        color: "#ff6600", pace: 51300 },
        { num: 48,  name: "M. Vohland",   bike: "KTM 450 SX-F",     brand: "KTM",        color: "#ff6600", pace: 51600 },
        { num: 92,  name: "A. Cairoli",   bike: "Ducati Desmo450",  brand: "Ducati",     color: "#cc0000", pace: 51900 }
    ];
    // Open a clear gap behind the lead pair (94/96) so the field settles into a lead
    // battle AND at least one separate chase pack, instead of one bunched train. The
    // director then cuts between DISTINCT battles as the spectate cycle moves through the
    // order — which is exactly what the overlay's horizontal battle→battle wipe exists to
    // show (and what tests/web/overlay.spec.js asserts). Shifting everything behind P2 by
    // a fixed amount preserves the midfield's internal spacing (and its own battles).
    for (var ri = 2; ri < roster.length; ri++) roster[ri].pace += 700;

    var sim;

    function pushEvent(type, message, detail, T) {
        sim.events.push({
            type: type, message: message, detail: detail,
            clockTime: clockNowFull(),
            clockMs: Date.now(),
            sessionTime: formatMmSs(T)
        });
        while (sim.events.length > 12) sim.events.shift();
    }

    // resetSim() starts the WARMUP phase; startRace() transitions to the race;
    // a finished race loops back to resetSim() after a short hold.
    function resetSim() {
        sim = {
            phase: "warmup",
            T: 0, events: [], leaderNum: -1,
            bestOverall: 0, bestOverallNum: -1,
            specIndex: 0, specMs: 0, finished: false, finishLap: -1,
            riders: roster.map(function (r, idx) {
                // Wander phase. The top two pairs get a SHARED phase per pair so each
                // drifts in lockstep and stays a stable, persistent battle (lead pair
                // 94/96, chase pair 32/27) rather than churning in and out of a group;
                // combined with the pace gap opened behind P2 above, that gives the
                // spectate cycle two DISTINCT battles to cut between (the battle→battle
                // wipe). Everyone else keeps a random phase for natural midfield mixing.
                var phase = (idx < 2) ? 0.6 : (idx < 4) ? 2.4 : Math.random() * Math.PI * 2;
                return {
                    num: r.num, fullName: r.name, bike: r.bike, brand: r.brand,
                    color: r.color, pace: r.pace, phase: phase,
                    dist: 0, laps: 0, lastLapMs: 0, bestLapMs: 0, gridPos: 0,
                    // Per-lap history (oldest-first {t,v}) so the session-charts
                    // carousel has real data to derive from under ?demo.
                    lapHistory: [],
                    // Position snapshots for the +/- column: at the last S/F
                    // crossing (Lap reference) and last sector change (Sector ref).
                    posAtSf: 0, posAtSplit: 0, prevSector: 0, crossedSf: false
                };
            })
        };
        pushEvent(0, "Warmup started", "", 0);
    }

    // Warmup -> race: grid by warmup best lap (riders with no lap go to the back),
    // then reset per-session lap data and restart the clock for the timed race.
    function startRace() {
        var grid = sim.riders.slice().sort(function (a, b) {
            return (a.bestLapMs || Infinity) - (b.bestLapMs || Infinity);
        });
        for (var i = 0; i < grid.length; i++) {
            grid[i].gridPos = i + 1;
            grid[i].posAtSf = grid[i].posAtSplit = i + 1;  // seed +/- references to the grid
            grid[i].dist = 0; grid[i].laps = 0;
            grid[i].lastLapMs = 0; grid[i].bestLapMs = 0;
            grid[i].lapHistory = [];
            grid[i].prevSector = 0; grid[i].crossedSf = false;
        }
        sim.phase = "race";
        sim.T = 0; sim.leaderNum = -1;
        sim.bestOverall = 0; sim.bestOverallNum = -1;
        sim.finished = false; sim.finishLap = -1;
        pushEvent(0, "Race started", "", 0);
    }

    // Lap pace wanders ±1.5% per rider so the order shuffles over time.
    function effPace(r, T) {
        return r.pace * (1 + 0.015 * Math.sin(2 * Math.PI * T / 110000 + r.phase));
    }

    function tick() {
        var dT = TICK_MS * SPEED;
        sim.T += dT;
        var T = sim.T;

        // Advance every rider + record lap / best-lap times (shared by both phases).
        for (var i = 0; i < sim.riders.length; i++) {
            var r = sim.riders[i];
            var p = effPace(r, T);
            var prevLaps = r.laps;
            r.dist += dT / p;
            r.laps = Math.floor(r.dist);
            r.crossedSf = (r.laps > prevLaps && r.laps > 0);
            if (r.crossedSf) {
                var lt = Math.round(p + (Math.random() * 600 - 300));
                r.lastLapMs = lt;
                r.lapHistory.push({ t: lt, v: 1 });   // demo laps are all valid
                if (r.bestLapMs === 0 || lt < r.bestLapMs) r.bestLapMs = lt;
                if (sim.bestOverall === 0 || lt < sim.bestOverall) {
                    sim.bestOverall = lt;
                    sim.bestOverallNum = r.num;
                    pushEvent(4, "Fastest lap — " + r.fullName, formatLapTime(lt), T);
                }
            }
        }

        // Rotate the spectated rider every ~12s so the focus card cycles (both phases).
        sim.specMs += TICK_MS;
        if (sim.specMs >= 12000) {
            sim.specMs = 0;
            sim.specIndex = (sim.specIndex + 1) % Math.min(6, sim.riders.length);
        }

        if (sim.phase === "warmup") tickWarmup(T);
        else tickRace(T);
    }

    // Demo fixture only: the live overlay gets battle groups from the plugin
    // (snapshot.battles). Here we synthesize the same shape (arrays of race
    // numbers) from the demo standings so the panel previews, mirroring the
    // plugin defaults (2.5s gap, top 10). NOT caster config - just demo data.
    function demoBattles(standings) {
        if (!standings) return [];
        var thr = 2500, topN = 10, groups = [], a = 0;
        while (a < standings.length) {
            var b = a;
            while (b + 1 < standings.length) {
                var cur = standings[b], next = standings[b + 1];
                if (((next.gapLaps || 0) - (cur.gapLaps || 0)) === 0 &&
                    ((next.gapMs || 0) - (cur.gapMs || 0)) > 0 &&
                    ((next.gapMs || 0) - (cur.gapMs || 0)) <= thr) b++;
                else break;
            }
            if (b > a) {
                if (standings[a].pos <= topN) {
                    var g = [];
                    for (var k = a; k <= b; k++) g.push(standings[k].num);
                    groups.push(g);
                }
                a = b + 1;
            } else { a = a + 1; }
        }
        return groups;
    }

    // Per-rider lap series for the session-charts carousel, in the classification
    // `order` given. Mirrors the plugin's snapshot.laps (num + oldest-first times);
    // demo laps are all valid so the `v` array is omitted, like the plugin.
    function demoLaps(order) {
        var out = [];
        for (var i = 0; i < order.length; i++) {
            var r = order[i];
            if (r.lapHistory && r.lapHistory.length) {
                out.push({ num: r.num, t: r.lapHistory.map(function (h) { return h.t; }) });
            }
        }
        return out;
    }

    // Synthetic director block so the focus-card story banner previews under ?demo.
    // Derives a shot from the spectated rider's situation (fastest-lap holder ->
    // "fastest"; close to the rider ahead -> "battle"; else "solo"); pace-setter in a
    // non-race -> "pace" when they own the best lap.
    function demoDirector(standings, specNum, isRace) {
        var idx = -1;
        for (var i = 0; i < standings.length; i++) if (standings[i].num === specNum) { idx = i; break; }
        if (idx < 0) return null;
        var shot = "solo", withNum = -1, paceSplit = -1;
        if (sim.bestOverallNum === specNum) {
            shot = isRace ? "fastest" : "pace";
            if (shot === "pace") paceSplit = idx % 3;  // cycle S1 / S2 / S3 for the preview
        } else if (isRace && idx > 0) {
            var ahead = standings[idx - 1];
            if ((ahead.gapLaps || 0) === (standings[idx].gapLaps || 0) &&
                ((standings[idx].gapMs || 0) - (ahead.gapMs || 0)) < 1500) {
                shot = "battle"; withNum = ahead.num;
            }
        }
        return { on: true, active: true, subject: specNum, with: withNum, shot: shot, paceSplit: paceSplit, camera: "Trackside" };
    }

    // WARMUP: classify by best lap (no-lap riders last), show the lap-time column
    // and no +/- column (render() hides it for non-race sessions).
    function tickWarmup(T) {
        var order = sim.riders.slice().sort(function (a, b) {
            return (a.bestLapMs || Infinity) - (b.bestLapMs || Infinity);
        });
        var specNum = order[Math.min(sim.specIndex, order.length - 1)].num;

        var standings = [];
        for (var k = 0; k < order.length; k++) {
            var rr = order[k];
            var chips = [];
            if (sim.bestOverallNum === rr.num && rr.bestLapMs > 0) chips.push("fastest");
            if (rr.num === specNum) chips.push("camera");
            standings.push({
                pos: k + 1, num: rr.num, name: rr.fullName, fullName: rr.fullName,
                bike: rr.bike, brand: rr.brand, brandColor: rr.color,
                plateColor: (rr.num === DEMO_TRACKED_NUM ? DEMO_TRACKED_PLATE : undefined),
                gap: "", gapMs: 0, gapLaps: 0,
                state: 0, numLaps: rr.laps, inPit: false, penalty: 0,
                bestLapMs: rr.bestLapMs, lastLapMs: rr.lastLapMs,
                idealLapMs: rr.bestLapMs > 0 ? rr.bestLapMs - 400 : 0, finished: false,
                posDeltaStart: 0, posDeltaSf: 0, posDeltaSplit: 0,
                chips: chips
            });
        }

        // Synthetic best-sectors for the panel preview: hand the fastest riders a
        // sector each, with a time split from their best lap. (Live, the plugin
        // sends real per-rider sector bests; here we fake it from bestLapMs.)
        var demoSectors = [];
        var withLaps = order.filter(function (r) { return r.bestLapMs > 0; });
        if (withLaps.length) {
            var frac = [0.33, 0.34, 0.33];
            for (var si = 0; si < 3; si++) {
                // Per-rider sector = a fraction of their best lap, with a small
                // deterministic per-rider/per-sector jitter so the sector rankings
                // differ from each other (and from the overall lap order).
                var riders = withLaps.map(function (r) {
                    var jitter = 1 + (((r.num * (si + 1)) % 7) - 3) * 0.01;   // ±3%
                    return { num: r.num, ms: Math.round(r.bestLapMs * frac[si] * jitter) };
                }).sort(function (a, b) { return a.ms - b.ms; }).slice(0, 8);
                demoSectors.push({ s: si + 1, riders: riders });
            }
        }

        var remaining = Math.max(0, WARMUP_LEN - T);
        render({
            session: {
                time: formatMmSs(remaining), timeMs: remaining,
                type: "Warmup", state: "In Progress", format: formatMmSs(WARMUP_LEN),
                numLaps: 0, sessionLength: WARMUP_LEN, isRace: false,
                isSpectating: true, trackName: "Demo National", trackLength: 1600,
                leaderLap: 0, compactTimes: true, pluginVersion: "demo"
            },
            standings: standings,
            sectors: demoSectors,
            laps: demoLaps(order),
            director: demoDirector(standings, specNum, false),
            events: sim.events.slice()
        });

        if (remaining <= 0) { if (FORCE_WARMUP) resetSim(); else startRace(); }
    }

    // RACE: classify by distance, gap-to-leader column, +/- snapshots, overtime.
    function tickRace(T) {
        var order = sim.riders.slice().sort(function (a, b) { return b.dist - a.dist; });
        var leader = order[0];
        if (sim.leaderNum !== -1 && leader.num !== sim.leaderNum) {
            pushEvent(15, "Lead change", leader.fullName, T);
        }
        sim.leaderNum = leader.num;

        var specNum = order[Math.min(sim.specIndex, order.length - 1)].num;

        // Update +/- reference snapshots now that classification (order) is known:
        // the Lap reference snaps each rider's position at the S/F line, the Sector
        // reference at each sector change (3 sectors/lap).
        var DEMO_SECTORS = 3;
        for (var oi = 0; oi < order.length; oi++) {
            var od = order[oi];
            var pos = oi + 1;
            var sector = Math.floor((od.dist - od.laps) * DEMO_SECTORS);
            if (od.crossedSf) od.posAtSf = pos;
            if (sector !== od.prevSector) { od.posAtSplit = pos; od.prevSector = sector; }
        }

        var standings = [];
        for (var k = 0; k < order.length; k++) {
            var rr = order[k];
            var gd = leader.dist - rr.dist;          // laps behind the leader
            var gLaps = Math.floor(gd + 1e-9);
            var gMs = Math.round((gd - gLaps) * effPace(leader, T));
            var chips = [];
            if (sim.bestOverallNum === rr.num) chips.push("fastest");
            // "camera" chip is added below, once the director subject is chosen.
            standings.push({
                pos: k + 1, num: rr.num, name: rr.fullName, fullName: rr.fullName,
                bike: rr.bike, brand: rr.brand, brandColor: rr.color,
                plateColor: (rr.num === DEMO_TRACKED_NUM ? DEMO_TRACKED_PLATE : undefined),
                gap: "", gapMs: (k === 0 ? 0 : gMs), gapLaps: (k === 0 ? 0 : gLaps),
                // Live gaps: same leader-relative value as the official gap here
                // (the sim has no separate real-time model), marked valid for
                // riders on the lead lap so the battle card exercises the live
                // path; a lapped rider is invalid, matching the plugin contract.
                liveGapMs: (k === 0 ? 0 : gMs), liveGapValid: (gLaps === 0),
                state: 0, numLaps: rr.laps, inPit: false, penalty: 0,
                bestLapMs: rr.bestLapMs, lastLapMs: rr.lastLapMs,
                idealLapMs: rr.bestLapMs > 0 ? rr.bestLapMs - 400 : 0,
                // Finished once past the (time+bonus) finish lap — drives the
                // checkered chip and the session-charts auto-show (fires when P1 finishes).
                finished: (sim.finishLap >= 0 && rr.laps > sim.finishLap),
                posDeltaStart: rr.gridPos - (k + 1),
                posDeltaSf: rr.posAtSf - (k + 1),
                posDeltaSplit: rr.posAtSplit - (k + 1),
                chips: chips
            });
        }

        // Camera / director. Feature BATTLES and cut between DISTINCT ones so the
        // overlay's battle-panel horizontal wipe is exercised, while still dropping to a
        // solo rider every few steps so the focus card keeps varying. The featured battle
        // is held stable BETWEEN spec steps by its FRONT rider's identity (riders joining/
        // leaving a battle don't count as a new one); each new spec step advances to a
        // DIFFERENT battle group, which is a genuine battle→battle cut. Falls back to the
        // running-order subject when there aren't two battles to cut between.
        var battles = demoBattles(standings);
        if (sim.specIndex !== sim.lastFeatIdx) {          // a spec-cycle boundary
            sim.lastFeatIdx = sim.specIndex;
            sim.featRot = (sim.featRot || 0) + 1;
            sim.featFront = (battles.length >= 2 && (sim.featRot % 4) !== 3)
                ? battles[sim.featRot % battles.length][0]   // a battle front (cut target)
                : -1;                                        // solo step -> focus card
        }
        var featGroup = null;
        if (sim.featFront >= 0) {
            for (var bfi = 0; bfi < battles.length; bfi++) {
                if (battles[bfi].indexOf(sim.featFront) >= 0) { featGroup = battles[bfi]; break; }
            }
        }
        var dir;
        if (featGroup) {
            specNum = featGroup[0];
            dir = { on: true, active: true, subject: featGroup[0],
                    with: (featGroup[1] != null ? featGroup[1] : -1),
                    shot: "battle", paceSplit: -1, camera: "Trackside" };
        } else {
            dir = demoDirector(standings, specNum, true);   // solo/pace -> focus card
        }
        for (var ci = 0; ci < standings.length; ci++) {     // camera chip on the subject
            if (standings[ci].num === specNum) { standings[ci].chips.push("camera"); break; }
        }

        // Time + 2 bonus laps. Until the clock expires, count it down; after
        // that, mirror the plugin's overtime label off the leader's laps so the
        // demo previews the "N TO GO" -> "FINAL LAP" -> "CHECKERED" sequence.
        // The clock holds at 00:00 while the leader finishes the lap in progress
        // at expiry (toGo > DEMO_BONUS); the countdown only starts once they
        // cross S/F into the bonus laps — same as the plugin.
        var DEMO_BONUS = 2;
        var remaining = Math.max(0, SESSION_LEN - T);
        var clock = formatMmSs(remaining);
        if (remaining <= 0) {
            if (sim.finishLap < 0) sim.finishLap = leader.laps + DEMO_BONUS;
            var toGo = sim.finishLap - leader.laps + 1;   // 1 = final lap
            if (leader.laps > sim.finishLap) clock = "CHECKERED";
            else if (toGo <= DEMO_BONUS) clock = (toGo <= 1) ? "FINAL LAP" : (toGo + " TO GO");
            // else toGo > DEMO_BONUS: in-progress lap — leave clock at "00:00"
        }
        render({
            session: {
                time: clock, timeMs: remaining,
                type: "Race", state: "In Progress", format: "12:00 + 2L",
                numLaps: DEMO_BONUS, sessionLength: SESSION_LEN, isRace: true,
                isSpectating: true, trackName: "Demo National", trackLength: 1600,
                leaderLap: leader.laps, compactTimes: true, pluginVersion: "demo"
            },
            standings: standings,
            battles: battles,
            laps: demoLaps(order),
            director: dir,
            events: sim.events.slice()
        });

        // ?charts preview: keep the session-charts panel forced up (it self-terminates
        // through its pages, then re-appears). Wall-clock throttle (survives the demo's
        // race-loop resets) so it re-fires only after each showing has run its course.
        if (FORCE_CHARTS && leader.laps >= 1 && Date.now() >= nextChartsForce) {
            forceSlot("charts");
            nextChartsForce = Date.now() + 6000;
        }

        // Once the leader takes the checkered, hold it briefly then loop. With ?race,
        // loop straight back into a fresh race (skip the warmup) so it stays on the race.
        if (remaining <= 0 && sim.finishLap >= 0 && leader.laps > sim.finishLap && !sim.finished) {
            sim.finished = true;
            setTimeout(function () { resetSim(); if (FORCE_RACE) startRace(); }, 6000);
        }
    }

    resetSim();
    if (FORCE_RACE) startRace();   // jump straight into the race, skipping the warmup
    setInterval(tick, TICK_MS);
}

// --- Initialize ---
if (/[?&#]demo\b/i.test(location.search + location.hash)) {
    startDemo();
} else {
    overlay.classList.add("disconnected");
    connect();
}
