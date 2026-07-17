# Plugin API test coverage

The goal is systematic coverage of the whole plugin API surface, with gaps
**visible** rather than incidental. This manifest tracks every game-facing DLL
export plus the internal actions (reachable via `MXBMRP3_Test_*` hooks in
`mxbmrp3/core/test_hooks.cpp`). Keep it honest — update it when you add a test.

Status legend:
- ✅ **asserted** — a test drives it and asserts its observable effect.
- 🟡 **driven** — exercised (perf/fuzz) but its output is not asserted.
- 🟠 **survival** — hit only by the callback fuzzer (must-not-crash), no correctness.
- ⚪ **untested** — not driven at all.
- ⛔ **n/a headless** — no observable effect the headless harness can assert.

## Game callbacks (`mxb_api.h` exports)

| Export | Observable effect | Status | Test |
|---|---|---|---|
| Startup | init; telemetry-rate return | ✅ | smoke |
| Shutdown | teardown; settings save; clean unload | ✅ | smoke / persist; teardown_test (Startup→…→Shutdown→unload after a busy session with the web server live is clean — no hang/leak/crash) |
| DrawInit | sprite/font counts | ✅ | smoke |
| Draw | render primitives (quads/strings) | 🟡 | smoke (count), perf (timing) — content not asserted |
| EventInit | track/player, session JSON | ✅ | race / sessions |
| EventDeinit | clears event state | ⚪ | — |
| RaceEvent | session type | ✅ | race / sessions |
| RaceDeinit | clears race state | ⚪ | — |
| RaceAddEntry | rider name/bike in standings | ✅ | race / sessions |
| RaceRemoveEntry | removes rider; clears per-rider maps | ✅ | sessions (reused-number reset) |
| RaceSession | session type/state/clock/length | ✅ | race / sessions |
| RaceSessionState | session state transitions (pre-start → green → race over) | ✅ | sessionstate_test (green snapshots the start grid → `posDeltaStart`; started/ended events) |
| RaceClassification | standings order, gaps, positions, penalties | ✅ | race / sessions |
| RaceClassification (overtime) | time+laps clock: finish-before-timer freeze + N TO GO/FINAL LAP/CHECKERED; `format` per race format | ✅ | session_format_test (getLeaderLapsToGo + formatSessionClock via `session.time`) |
| RaceCommunication | penalty / DNS / retired / DSQ | ✅ | race (DSQ) / sessions (penalty) |
| RaceLap | lap time, best lap, fastest-lap event, per-sector bests (`sectors[]`), ideal lap, player personal-best stats | ✅ | lap_test (last lap, `fastest` chip + event) · sectors_test (best-sectors ranking) · ideal_lap_test (`idealLapMs` = sum of best sectors) · stats_test (player PB persisted) |
| RaceSplit | **live** current-lap splits + `posDeltaSplit` (NOT the best-sectors board — that's RaceLap) | ✅ | posdelta_split_test (positions gained/lost since the last split). Live current-lap split *display* is in-game-only (🟠 fuzz) |
| RaceHoleshot | holeshot event | ⛔ | **intentionally not exported** — the game never fires it (`plugin_manager.cpp:458`); struct kept as vendor-API spec only |
| RaceTrackPosition | map positions, real-time gaps, lapper detection | ✅ | trackpos_test (leader gap via GetRealTimeGap; lapped rider → gap 0) · trackpos_stale_test (rider outside the ~10-closest batch keeps a frozen gap, not a stale-derived one — the leader-dropout corruption). Map geometry still ⛔ |
| RaceVehicleData | per-rider rpm/gear/lean | 🟠 | fuzz — **gap** |
| RunInit | player run start → stats session timers begin | ✅ | stats_test (drives `RunInit` to open the stats session) |
| RunDeinit | player run end → leave-track flush: player stats persisted (and settings, in prod) | ✅ | stats_test (drives `RunDeinit`; the deferred personal-best is written to the stats JSON only here, not mid-ride) |
| RunStart / RunStop | RunStart: sim resume (arms isPlayerRunning, which FMX gates on). RunStop: leave-track (pits) → settings flush + stats save | ✅ / 🟠 | fmx_test drives RunStart (FmxManager gates trick detection on isPlayerRunning). RunStop's prod-only pit-flush stays uncovered headless; the equivalent flush is asserted via the settings-defer hooks + RunDeinit |
| RunLap | player lap → lap log, personal best, ideal lap | ⛔ | **stub** — the handler is empty; the lap/PB logic runs in the player-gated RaceLap path (covered by stats_test) |
| RunSplit | player split → sector | 🟠 | fuzz — **gap** |
| RunTelemetry | player speed/rpm/gear/lean/fuel → widgets/helmet + stats (distance/top speed) + FMX | ✅ | odometer_test (distance integration over injected-clock ticks, ~100m dirty coalescing, non-finite rejection, >0.5s gap clamp, persisted total) · fmx_test (trick detection off orientation/contact frames) · stats_test (top speed + the `finiteOrZero` +Inf write guard) · perf. Widget/helmet rendering stays ⛔ (not in /api/state) |
| TrackCenterline | map geometry | 🟡 | perf; fuzz (crash — bug fixed). Geometry correctness ⛔ (map is rendered, not in JSON) |
| SpectateVehicles | spectate target (the `camera` chip) | ✅ | spectate_test (camera chip follows the spectated rider, moves on a camera cut; `isSpectating` too) |
| SpectateCameras | camera-name matching (director) | 🟠 | fuzz — **gap** |
| GetModID / GetModDataVersion / GetInterfaceVersion | constants | ⚪ | trivial |

## Internal actions (`MXBMRP3_Test_*` hooks)

| Action | Effect | Status | Test |
|---|---|---|---|
| CompareVersions | update-checker version ordering | ✅ | version_test |
| ResetAll | factory-reset per-profile HUDs; globals untouched | ✅ | reset_test (scope asserted: HUDs revert, Rumble/Hotkeys stay) |
| ResetActiveProfile | factory-reset active profile only | ✅ | reset_profile_test (clears the active-profile diff; base + other profiles untouched) |
| ResetHud | factory-reset one HUD ("reset tab"), keep visibility | ✅ | reset_profile_test (clears only the named HUD's diff; another HUD's survives) |
| CopyProfileToAll | copy active profile to all profiles | ✅ | reset_profile_test (the active diff propagates to every profile) |
| SwitchProfile | switch active profile (also profile-scope tests) | ✅ | reset_profile_test (active profile persists, diffs preserved) |
| SetAutoSwitch / GetActiveProfile | arm auto-by-session switching; read the active profile (not in JSON) | ✅ | autoswitch_test |
| DirectorSetEnabled / DirectorToggleLock / DirectorIsLocked | enable the auto-director; toggle + read the rider lock | ✅ | director_lock_test (lock survives a standings update, releases on a session change) |
| DirectorNextLockedCamera | next camera role in the rider-lock rotation (deterministic given the cam pool) | ✅ | director_lock_test (TV → onboards → wrap; off-pool role restarts at TV) |
| DirectorSetNowMs | inject a simulated wall-clock so the director's shot pacing plays out under a headless tape replay | ✅ | director_broadcast_test (drives the sim clock from tape timestamps) |
| FmxSetNowUs / FmxState | inject the FMX wall-clock (dt integration, debounces, grace/chain windows — `Fmx::clockNow()`); read the score/chain/active-trick state (in-game-only, not in JSON) | ✅ | fmx_test (backflip detected → graced → banked; hop debounce; grace-crash fail) |
| StatsSetNowUs / StatsOdometerState / StatsSave | inject the odometer wall-clock (`StatsManager::odometerNow()`); read the live odometer/trip + the dirty-coalescing internals; force a stats save (clean baseline) | ✅ | odometer_test (distance integration + ~100m coalescing + non-finite rejection) |
| MarkDirty / FlushIfDirty / IsDirty / SetAutoSave | deferred settings-save model: mark unsaved (no write); flush on leave-track; read the unsaved-changes state; toggle Auto-Save | ✅ | settings_defer_test (markDirty writes nothing; flushIfDirty persists once; Auto-Save off → manual Save only) |
| SetActiveTab / GetActiveTab | set/read the focused settings tab (by name) — the persisted-tab seam | ✅ | settings_tab_test (round-trips through save/load; unknown name is a no-op) |
| ExtractAndInstall | run the update backup→extract→verify→rollback pipeline against a temp dir with an in-memory zip (bypasses the WinHTTP download) | ✅ | updater_test (happy install; locked target aborts intact; transient lock recovered by retry) |
| StartHttp | start the embedded HTTP server (serving-path tests) | ✅ | http_test / http_robust_test |
| AnalyticsPrime / SetFullLaunch / AppStarted / QueueSessionEnd / QueueCustom / SeedCrash / DrainPending | dry-run capture seam: fake identity + capture mode (senders no-op), then build app_started / run the gated + crash paths and drain what they'd send | ✅ | analytics_wiring_test |
| Save | force a settings save | ✅ (used by persist/reset/defer) | persist / reset_test / settings_defer_test (manual Save writes even with Auto-Save off) |
| LoadSettings | (re)load an INI from disk into live state | ✅ (the live-state seam) | reset_test / settings_tab_test / settings_idempotency_test / settings_apply_values_test |
| CapturedSections | the section names `captureToCache()` produces (sorted) — the seam for the per-HUD registry / serialize-consistency guard | ✅ | settings_sections_test |
| GetRealTimeGap | read a rider's computed live gap (internal, not in JSON) | ✅ | trackpos_test |
| PluginThreadEnable / PluginThreadEnabled / SetPluginThreadFlag / PluginThreadFlush / PluginThreadStop | drive the `[Advanced] pluginThread=1` worker: start it, read whether it's running, flip the flag (the reconcile-on-Draw path), barrier-flush the queue, stop it | ✅ | plugin_thread_test (off-thread equivalence) / plugin_thread_golden_test (real-tape identity) / plugin_thread_switch_test (live legacy↔threaded toggle) / plugin_thread_teardown_test (Shutdown joins + drains) |
| SetProduceDelayMs | inject a per-frame stall into the shared render build (`HudManager::produceFrame`) — the "slow component" stand-in | ✅ | plugin_thread_latency_test (sync Draw pays the stall; threaded Draw doesn't; perf metrics stay live) |
| XInputStopIo / XInputSetIndex / XInputVibrate / XInputConsumePending | XInput I/O-thread seam: stop the I/O thread, select a controller slot, post a rumble via `setVibration()`, inspect the posted (undrained) command | ✅ | xinput_thread_test (send policy + 8-bit quantization preserved off-thread) |
| Snapshot | build `/api/state` directly (no server/socket/gating) — the isolation seam for logic tests | ✅ | all logic tests via `host.snapshot()` |

Note: the per-profile resets clear the **profile diff**, not the shared **base**
INI section (a base edit legitimately survives them — the "m_hudDefaults is not a
clean factory snapshot" property). `reset_profile_test` exercises them by
perturbing a `[HudName:Profile]` diff; the same profile-diff seed is how
`copyProfileToAll`/`switchProfile` should be asserted next.

## Internal features & user actions ("what users actually do")

Beyond the game→plugin callbacks, the plugin has user-initiated actions and
derived features. A feature is headless-testable iff its effect reaches an
observable sink: **A** = JSON snapshot, **B** = persisted file, **C** = a pure
decision function, **A'** = a dry-run *capture* seam (a test-build hook that
intercepts a payload just before its external send — e.g. the analytics event
bodies — so the "what would we send" is assertable without the network). Effects
that only reach **D** (pixels / rumble vibration / Discord-Steam presence / the
live network round-trip itself) aren't headless-assertable — for those we test the
*decision* (C) or the *captured payload* (A') and leave the external half to
manual in-game.

| Feature / action | Initiator | Sink | Testable | Status / plan |
|---|---|---|---|---|
| Toggle HUD / widget on/off | user | B + A | ✅ | persist round-trip ✅; extend to JSON effect |
| Change a setting (gap mode, columns, opacity, position) | user | A / B | 🟡 | **apply + persistence** now asserted for specific non-default enum/float/int settings (settings_apply_values_test: gapMode / animationMode / riderColorMode / labelMode / displayRowCount / trackWidthScale / maxDisplayLaps survive load→apply→save via `[Hud:Practice]` overrides) and for **all** defaults (settings_idempotency_test: byte-identical save→load→save); plus settings_defer / settings_tab / persist round-trip. Remaining: a setting's **JSON snapshot** effect (e.g. does gapMode change `/api/state`) still not asserted directly |
| Settings capture/apply/serialize integrity (one registry) | derived | B | ✅ | settings_sections_test (every section `captureToCache()` produces is serialized — capture ⊆ serialized). The per-HUD serializer registry makes capture, apply, and serialize a single ordered list, so the "captured/applied but never written → silently reverts on restart" drift (the FriendsHud trap) is structural, not just caught |
| Settings save is **deferred** (off-track only) | user / derived | B | ✅ | settings_defer_test (a HUD drag/toggle only `markDirty`s — no write; the write happens once on the leave-track flush / manual Save; never mid-ride). The "no save while on track" contract |
| Settings menu remembers its **open tab** | user | B | ✅ | settings_tab_test (last-focused tab round-trips through save/load by name; an unknown/unavailable tab name is ignored — no empty tab) |
| Reset all / active-profile / one HUD | user | B | ✅ | Reset All (reset_test); active-profile + per-HUD scope (reset_profile_test) |
| Copy profile to all | user | B | ✅ | reset_profile_test (active diff propagates to all profiles) |
| Switch profile (manual + auto-by-session) | user / derived | B / A | ✅ | reset_profile_test (manual switch persists + preserves diffs); autoswitch_test (session type → active profile follows: Practice/Qualify/Race; disabling stops it overriding a manual pick) |
| Auto-director: **battle detection** (`battles[]`) | derived | **A** | ✅ | director_test (two engineered groups + gap break; negative-control verified) |
| Auto-director: **broadcast pacing** (cut rate, shot-length spread, field rotation, camera mix) | derived | C (cut log) | ✅ | director_broadcast_test (replays a real 24-rider tape under an injected sim-clock so the wall-clock pacing plays out, parses the director's own cut log, and asserts a plausible band: cuts>0 within bounds, avg shot inside the min/max envelope, ≥half the field gets airtime; a lone-rider session degrades without thrash) |
| Auto-director: active subject / camera / pace | derived | A (`director`) | 🟡 | subject rotation + camera mix now exercised by director_broadcast_test (via the sim-clock); the live-JSON `director` advisory block + the *pace* shot specifically still to assert |
| Auto-director: rider lock (hold) release rules | user / derived | C (hook) | ✅ | director_lock_test (survives standings churn; released by a session-generation bump). Release-on-manual-control + camera-roam-while-locked are manual (wall-clock cadence / manual-camera state) |
| Stats: odometer / distance / top speed / personal bests | derived | **B** (stats JSON) | ✅ | stats_test (personal-best lap persist + faster-replaces-only; top speed + `finiteOrZero` guard) · odometer_test (distance integration over injected-clock ticks; ~100m dirty coalescing; +Inf/NaN sample rejected; >0.5s gap contributes nothing; total persisted finite on leave-track). **Deferred**: the PB is NOT on disk mid-ride and is flushed on RunDeinit (the "no save while on track" rule) |
| Real-time gaps / lapper-ahead / position deltas | derived | A | ✅ | trackpos_test / trackpos_stale_test (leader gap, lapped→0, stale-batch freeze) |
| Overlay live gaps (battle cards) | derived / user | A | ✅ | livegaps_test (JSON `liveGapMs`/`liveGapValid`, always emitted) · tests/web (battle card renders live interval when `CONFIG.battleLiveGaps` on, official split when off). `battleInterval()` now falls back to the official split when the jittery live interval momentarily goes non-positive (was rendering an empty flash) — matches in-game, which never blanks a battling rider |
| Settings migration (old/version-less INI) | user/upgrade | B | ✅ | settings_migration_test (missing/`=4`/`=99` version → HUD settings survive) |
| Session clock / race-format string | derived | A | ✅ | session_format_test (laps/time/time+laps + overtime state machine) |
| HTTP server survival (slow/malformed clients) | external | A | ✅ | http_robust_test (slow-loris + garbage don't wedge server or game thread) |
| Whole-pipeline fidelity (real captured callback stream) | derived | A | ✅ | replay_golden_test (a real solo 1-lap MXB Club capture reconstructs its result) · replay_golden_multi_test (a real 24-rider Farm14 race: winner, gaps, fastest-lap chip on a non-winner, penalty, lapped, DSQ/DNS/retired). The fidelity anchor — proves the plugin processes the *exact* callback stream the game emits, not just hand-authored events |
| Replay machinery itself (tape read/dispatch) | harness | A | ✅ | replay_test (a `TapeWriter`-synthesized tape round-trips through `replayTape()` — no game needed) |
| Clean shutdown/unload after a busy session | lifecycle | survival | ✅ | teardown_test (web server live + full state → Shutdown → FreeLibrary is clean; guards the analytics-reported AV-on-unload class) |
| Plugin worker thread (`[Advanced] pluginThread=1`): callbacks routed off the game thread | user (INI) / derived | A | ✅ | plugin_thread_test (same synthetic race → same standings off-thread, `PluginThreadFlush` barrier before asserting) · plugin_thread_golden_test (a real ~8238-callback tape reconstructs the identical golden result — nothing dropped/reordered/raced across the queue) · plugin_thread_switch_test (runtime legacy↔threaded toggle: flip the flag, next Draw's `reconcileEnabled()` starts/stops the worker; standings correct across each switch) · plugin_thread_teardown_test (Shutdown with the worker live + a callback still queued: joined first, queue drained inline — no hang/use-after-free) · plugin_thread_latency_test (an injected `produceFrame` stall is paid by Draw in sync mode, not threaded — the feature's point). Real-game thread scheduling/pacing stays a manual in-game check |
| XInput I/O thread (blocking `XInputSetState` moved off-thread) | derived | C (posted command, via hook) | ✅ | xinput_thread_test (the rumble send policy — first-send, idle-silence, transition-to-zero, disabled-guard — and the 8-bit quantization stay on the caller in `setVibration()` and are asserted on the command it posts, with the I/O thread stopped so it can't drain the post first; the thread's start/stop/join lifecycle is exercised by every test's startup/shutdown). Rumble *feel* and the degraded-driver latency benefit stay manual (no controller under Wine) |
| Notices: masked-consumption (timed notice hidden behind a higher-priority banner) | derived | C (pure) | ✅ | test_notice_priority (unit): `NoticePriority::stepTimer` holds a masked consumable notice instead of counting it down + clearing it unseen; window starts on unmask; re-trigger restarts it |
| Auto-by-session profile switch | derived | B / A | ✅ | autoswitch_test (session type → active profile; off = no override) |
| FMX trick detection / scoring | derived | state (via hook) | ✅ | fmx_test: real RunTelemetry orientation/contact frames under the injectable FMX clock (`Fmx::clockNow()`, 10ms sim steps) — a sub-debounce hop banks nothing; a sustained airborne full-pitch rotation classifies as BACKFLIP, survives the landing grace, and its score banks into the session on chain expiry; a crash during grace fails it without touching the session score. State read via `MXBMRP3_Test_FmxState` (in-game-only, not in JSON) |
| Records fetch (MXB-Ranked / CBR) | user / auto | D (net) + C (parse) | ⚠️/✅ | **parse ✅** (records_parse_test: canned CBR/MXB-Ranked responses through the real parse path via `MXBMRP3_Test_RecordsParse` — field mapping, malformed/absurd inputs); **worker + shutdown-mid-fetch join contract ✅** (stubbed fetch via `MXBMRP3_Test_RecordsSetFetchStub`, no network); live fetch (WinHTTP transport against the real endpoints) manual |
| Rumble effects | derived | D (feel) + C (math) | ✅ | rumble_effect_test: the telemetry→vibration effect math through the real RunTelemetry path — zero telemetry is exactly silent, the wheelspin/lockup ramps map the handler-derived slip ratios, a suspension spike drives Bumps scaled by the per-bike PROFILE JSON's strength (and doubling it doubles the output), airborne suppresses ground effects, malformed profile JSON falls back without crashing. Channels read via `MXBMRP3_Test_RumbleChannels` (in-game-only). Vibration *feel* stays manual (no controller under Wine) |
| Discord / Steam presence | derived | D + C (string) | ⚠️ | presence string via hook; appearance manual |
| Update check / download | user / auto | C + D | ⚠️/✅ | version compare ✅; **install pipeline** ✅ (updater_test: backup→extract→verify→rollback + locked-file retry); live download manual |
| Analytics: remote sampling **cost lever** | dev / derived | C (pure) + A' (capture) | ✅ | test_analytics_remote_config (unit): `parseFullSample`/`shouldSendFull` — fail-open to full, clamping, deterministic 0.0/1.0 endpoints. analytics_wiring_test drives the **gate** (below). The live WinHTTP fetch stays manual |
| Analytics: **event wiring** (payload + gate) via the dry-run capture seam | derived | A' (capture) | ✅ | analytics_wiring_test — app_started is the always-sent tier (anon id + feature flags + `isDebug`); a FULL launch enqueues session_end + custom, a MINIMAL launch drops both, crash bypasses the gate. Analytics is now compiled into the test DLL (auto-init stays off, `GAME_HAS_ANALYTICS=0`); capture mode makes the real senders no-ops so a test build never phones home |
| Analytics: App-Key **region routing** | derived | C (pure) | ✅ | test_analytics_endpoint (unit): `aptabaseHostForKey` — US/EU route to their ingest hosts; self-hosted/empty/malformed/stray-region → "" (no send). A wrong mapping silently sends every event to the wrong region |
| Hotkeys (toggle / cycle / overlay-force / spectate) | user | triggers actions | ✅ | invoke the action hooks (actions are testable) |
| Mouse-drag reposition | user | B (position) | ⚠️ | position persists (testable); drag input manual |

Legend: ✅ fully headless-testable · ⚠️ decision logic testable, external half manual.

## Prioritised gaps (highest value first)

1. ~~**RaceLap**~~ — ✅ done (lap_test): last lap, fastest-lap chip + event, moves when beaten.
2. ~~**RaceTrackPosition**~~ — ✅ done (trackpos_test): real-time leader gap via a white-box hook (the live gap is internal, not in the JSON — so it's read directly, not through the snapshot).
3. ~~**Best-sectors board** (`sectors[]`)~~ — ✅ done (sectors_test): per-sector fastest-first ranking. (It's fed by RaceLap splits, not RaceSplit — a finding while writing it.) Remaining: **RaceSplit**'s own `posDeltaSplit` (race, positions gained since last split).
4. ~~**Reset active-profile / HUD scope + copy/switch**~~ — ✅ done (reset_profile_test): profile-diff perturbation clears the active-profile / named-HUD diff (base + non-active profiles untouched); copy-to-all propagates the active diff; switch persists the active profile and preserves diffs. Remaining: **auto-by-session** profile switch.
5. ~~**RaceSessionState** transitions~~ — ✅ done (sessionstate_test): green snapshots the grid (`posDeltaStart`), started/ended events.
6. ~~**SpectateVehicles**~~ — ✅ done (spectate_test): the `camera` chip follows the spectated rider. (**RaceVehicleData** rpm/gear/lean isn't emitted to the JSON — in-game only, so ⛔ for a black-box test.)
7. ~~**Personal bests / stats file, ideal lap, posDeltaSplit**~~ — ✅ done: stats_test (PB persist + faster-replaces-only, top speed + `finiteOrZero` guard), ideal_lap_test (`idealLapMs`), posdelta_split_test (positions gained since the last split). ~~Remaining: **distance/odometer** accumulation~~ — ✅ done (odometer_test, via the injectable odometer clock). (RaceHoleshot / RunLap are intentionally unexported/stub — not gaps.)

Callbacks with no headless-observable effect (RunStart/Stop, deinits, TrackCenterline
geometry, the widget/helmet half of RunTelemetry) are covered for **survival** by the
callback fuzzer and marked ⛔/🟠 above rather than left as silent gaps.

~~**Top remaining open gap: FMX (freestyle trick scoring) — whole subsystem untested.**~~
— ✅ done (fmx_test), exactly along the sketched seam: every FMX wall-clock read
(FmxManager's `dt`/grace/animation timing AND `FmxScore`'s chain timer) now goes through
one `Fmx::clockNow()` helper (fmx_types.h) whose `MXBMRP3_TEST_BUILD`-only override
mirrors `DirectorManager::testSetNowMs`; `MXBMRP3_Test_FmxSetNowUs` steps simulated time
10ms per telemetry frame and `MXBMRP3_Test_FmxState` exposes score/chain/active-trick.
The test drives real RunTelemetry orientation sequences and asserts a *detection*, not a
tautology: a sub-debounce hop banks nothing; a full-pitch airborne rotation classifies
as `BACKFLIP`, survives the 0.75s landing grace, and banks a non-zero score into the
session when the 2s chain window expires; a crash during grace fails it. The odometer
gap closed with the sibling seam (`StatsManager::odometerNow()` + `StatsSetNowUs`,
odometer_test).

## Known logic gaps needing a new seam (visible, not silent)

These are confirmed behaviours worth a test, but each needs a new observability
seam the harness doesn't have yet — recorded here so they're managed, not lost.

1. ~~**Updater — no retry/backoff on a locked/in-use target**~~ — ✅ done
   (updater_test): `moveFileWithRetry` now retries the backup/restore moves on a
   transient sharing/access lock (~1.5s backoff), and `restoreFromBackup` returns
   an accurate result. Tested via the `MXBMRP3_Test_ExtractAndInstall` hook
   against a temp dir with an in-memory zip (`zipwrite.h`): happy install; target
   locked for the whole attempt → abort with the original `.dlo` intact; transient
   lock released mid-attempt → the retry recovers and the install succeeds. The
   live WinHTTP download stays manual-only (no injection seam); `SHA256` /
   `mapToInstallPath` / `shouldSkipFile` still unit-testable via a future hook.
2. ~~**Notices — a timed notice can be consumed while masked**~~ — ✅ done. The
   consumable timed notices (all-time PB / fastest lap / session PB / default
   setup / segment) used to run their display countdown from the *event* time,
   independent of whether they were ever on screen — so a PB set on a lap where
   the player also briefly went WRONG WAY counted down behind the banner and was
   cleared unseen ("incorrect notices"). Fixed by measuring the window from the
   *unmask* moment: `hud/notice_priority.h`'s pure `stepTimer({pending, masked,
   triggerMs, unmaskAtMs}, now, duration)` HOLDS a masked notice (never shown,
   never consumed) and starts its full window when the mask clears; taking the
   later of trigger/unmask also preserves "a fresh re-trigger restarts the
   window". `notices_hud.cpp` computes the status-tier `statusMasking` flag and
   feeds it per consumable notice. Tested as pure logic in `tests/unit/`
   (test_notice_priority) — held-while-masked, window-from-unmask, mid-display
   mask, re-trigger restart, not-pending reset. (The in-game render ladder itself
   stays a manual check; the consumption *decision* is now unit-tested.)
