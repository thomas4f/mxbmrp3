// ============================================================================
// core/stats_manager_telemetry.cpp
// Everything StatsManager integrates at telemetry rate: the odometer and top
// speed, crash and gear-shift edges, fuel burnt, and the proximity seconds
// behind Roost / Side by Side. Split out of stats_manager.cpp verbatim when it
// crossed the file budget - the class definition and the export surface are
// unchanged.
//
// It is one TU because it is one contract: everything here runs on a ~30-100Hz
// path, integrates against the SAME injectable clock (odometerNow, which the
// headless tests drive through MXBMRP3_Test_StatsSetNowUs), and coalesces its
// achievement evaluation onto the odometer's ~100m mark rather than evaluating
// per tick. A new per-tick number belongs here, next to that cadence.
// ============================================================================

#include "stats_manager.h"
#include "achievement_manager.h"

#include <algorithm>
#include <cmath>

// Minimum speed to count as movement (filters out noise when stationary)
static constexpr float MIN_MOVEMENT_SPEED_MS = 0.1f;  // ~0.36 km/h
// How much measured time to gather before handing the proximity seconds to the
// exploration sums. The feed evaluates the catalogue, and the position batch it
// rides on arrives ~30 times a second.
static constexpr double PROXIMITY_FLUSH_SEC = 1.0;

// Digging a Hole. The bike is going nowhere (under DIG_MAX_SPEED_MS) while the
// rear wheel says otherwise by more than DIG_SLIP times the speed it is making
// - FmxManager's burnout test, restated here rather than borrowed, because a
// player who turns the Burnout trick off in the INI would otherwise turn this
// row off with it. The slip ratio divides by max(1, speed) for the same reason
// it does there: the interesting case is a speed of zero.
static constexpr float DIG_MAX_SPEED_MS = 2.0f;    // ~7 km/h: creeping, not riding
static constexpr float DIG_SLIP = 5.0f;
// How far apart two crashes can be along the centreline and still be "the same
// place" (Favorite Spot). A hundredth of a lap: fifteen metres on a 1.5km
// track, which is one corner and not two.
static constexpr float SAME_SPOT_FRACTION = 0.01f;

// Peace Out. "Ride through" is two claims, and the row has to hold both.
//
// RIDING, not creeping: the odometer's 0.1 m/s only says "not parked", and at
// 0.36 km/h a rider dabbing their way through a heap would earn a row about
// riding through one. Five metres a second is 18 km/h - slow for a motocross
// track, quick enough that nobody gets there by paddling.
//
// AND NOT YOUR OWN HEAP. Nothing else stops the first-corner case: go down,
// remount while the others are still on the floor, ride off, and the SAME
// incident credits Pile-Up and Peace Out both - the pair is meant to be
// exclusive. So the count is skipped for a spell after the player's own crash
// was last seen down, long enough that anyone genuinely riding has left their
// own crash site behind: ten seconds at the speed above is fifty metres, five
// times the radius the heap is measured over.
static constexpr float PEACE_OUT_MIN_SPEED_MS = 5.0f;
static constexpr double PEACE_OUT_AFTER_CRASH_SEC = 10.0;

// Distance between two centreline positions, the short way round: a crash at
// 0.999 and one at 0.001 are two metres apart, not a lap.
static float trackPosGap(float a, float b) {
    float d = std::fabs(a - b);
    return (d > 0.5f) ? 1.0f - d : d;
}

#if defined(MXBMRP3_TEST_BUILD)
// Injectable simulated clock for the headless odometer test. -1 = real
// steady_clock (production path). Never compiled into a shipping DLL.
static long long s_statsTestNowUs = -1;
void StatsManager::testSetNowUs(long long us) { s_statsTestNowUs = us; }
#endif

std::chrono::steady_clock::time_point StatsManager::odometerNow() {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_statsTestNowUs >= 0) {
        return std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::microseconds(s_statsTestNowUs)));
    }
#endif
    return std::chrono::steady_clock::now();
}

void StatsManager::setTankCapacity(float litres) {
    m_tankCapacityL = (std::isfinite(litres) && litres > 0.0f) ? litres : 0.0f;
    // A new bike means a new tank: the previous bike's level is not a
    // reference for this one, and differencing across them would read as a
    // huge burn (or be thrown away by the sanity test, which is worse - it
    // would silently drop the first real burn of the session).
    m_hasLastFuel = false;
}

void StatsManager::recordRidersDown(int down) {
    // The player is on the floor RIGHT NOW - this is only called from the
    // branch that established it - so this is also the moment Peace Out's guard
    // measures from. Stamped here rather than off updateTelemetry's crash edge,
    // which reads the crash flag the PREVIOUS position batch left behind: one
    // batch of the ride away from your own heap then beat the guard into place
    // and credited it. Both halves of the pair now come off the same batch.
    m_lastSeenDownTime = odometerNow();
    m_hasSeenDownTime = true;
    m_exploration.onRidersDown(down);
}

void StatsManager::recordRodeThrough(int down) {
    if (m_lastSpeedMs < PEACE_OUT_MIN_SPEED_MS) return;   // parked or paddling is not through it
    if (m_hasSeenDownTime) {
        const double since = std::chrono::duration_cast<std::chrono::microseconds>(
                                 odometerNow() - m_lastSeenDownTime).count() / 1000000.0;
        if (since < PEACE_OUT_AFTER_CRASH_SEC) return;    // still your own crash site
    }
    m_exploration.onRodeThrough(down);
}

void StatsManager::recordProximity(Roost::Contact contact, bool measuring) {
    // RIDE MEANS MOVING, the same rule the ride rows follow and on the same
    // threshold the odometer uses. Without it two bikes stopped on track within
    // a few metres of each other banked roost for as long as they sat there -
    // the pre-start grid was already out (it is not IN_PROGRESS), a mid-race
    // stop was not. A stop ENDS the interval rather than pausing it, like every
    // other break here.
    if (m_lastSpeedMs < MIN_MOVEMENT_SPEED_MS) measuring = false;
    if (!measuring) {
        // End the interval rather than pause it: resuming across a menu or a
        // crash would credit the whole gap as time spent racing someone.
        m_hasLastProximityTime = false;
        return;
    }
    const auto now = odometerNow();
    if (!m_hasLastProximityTime) {
        m_lastProximityTime = now;
        m_hasLastProximityTime = true;
        return;                                  // no interval to credit yet
    }
    const double dt = std::chrono::duration_cast<std::chrono::microseconds>(
                          now - m_lastProximityTime).count() / 1000000.0;
    m_lastProximityTime = now;
    // The odometer's window, for the odometer's reason: a longer gap is a pause
    // or a stall, and crediting it would hand out minutes for standing still.
    if (!(dt > 0.0 && dt <= 0.5)) return;

    // SideBySide is still CLASSIFIED -- it is how the detector says "alongside,
    // not in the roost", which is what stops a rider beside you crediting roost
    // time -- it is simply no longer counted. Its achievement is gone.
    switch (contact) {
        case Roost::Contact::Roost:      m_roostPendingSec += dt; break;
        case Roost::Contact::SideBySide: break;
        case Roost::Contact::None:       break;
    }

    m_proximitySinceFlushSec += dt;
    if (m_proximitySinceFlushSec < PROXIMITY_FLUSH_SEC) return;
    m_proximitySinceFlushSec = 0.0;
    if (m_roostPendingSec > 0.0) {
        m_exploration.onProximityTime(m_roostPendingSec);
        m_roostPendingSec = 0.0;
    }
}

void StatsManager::updateTelemetry(float speedMs, bool isCrashed, int currentGear, float fuelLitres,
                                   float rearWheelSpeedMs, float trackPos) {
    // Sanitize the speed sample: NaN is rejected by the comparisons below
    // anyway, but +Inf passes them and would poison the odometer / top-speed
    // values, which are PERSISTED - one bad physics sample would corrupt the
    // stats file with no recovery path.
    if (!std::isfinite(speedMs)) {
        speedMs = 0.0f;
    }
    m_lastSpeedMs = speedMs;

    // Single lookup for the entire method — setCurrentContext() guarantees entry exists
    TrackBikeStats* stats = nullptr;
    if (!m_currentKey.empty()) {
        auto it = m_trackBikeStats.find(m_currentKey);
        if (it != m_trackBikeStats.end()) stats = &it->second;
    }

    // Crash edge detection — only count rising edges (not-crashed -> crashed)
    if (isCrashed && !m_wasCrashed) {
        // The TALLY first, and OUTSIDE the `stats` guard below. That guard exists
        // because the per-track+bike record needs a track and a bike to be filed
        // under; the tally needs neither -- it is a count of crashes, full stop --
        // and a crash landing before setCurrentContext() has run would otherwise
        // go uncounted on the one number a viewer is watching.
        m_globalStats.crashTally++;
        m_dirty = true;
        if (stats) {
            stats->crashCount++;
            if (!m_globalTotalsDirty) ++m_cachedTotalCrashes;   // clean cache stays exact (see the members)
            m_sessionCrashes++;
            m_curLapCrashes++;
        }
        m_exploration.onCrash(m_sessionCrashes, m_globalStats.crashTally);
        // Favorite Spot: the same place, again. A RUN, so a crash anywhere else
        // starts the count over at one rather than adding to a tally - three
        // scattered over an afternoon is a hard track, three in a row is a
        // corner with your name on it.
        if (trackPos >= 0.0f && trackPos <= 1.0f) {
            if (m_sameSpotCrashRun > 0 &&
                trackPosGap(trackPos, m_lastCrashTrackPos) <= SAME_SPOT_FRACTION) {
                ++m_sameSpotCrashRun;
            } else {
                m_sameSpotCrashRun = 1;
                m_lastCrashTrackPos = trackPos;
            }
            m_exploration.onCrashSpotRun(m_sameSpotCrashRun);
        }
        AchievementManager::getInstance().onStatsChanged();
    }
    m_wasCrashed = isCrashed;

    // Digging a Hole: an unbroken run of standing still with the rear wheel
    // spinning. Its own clock, because the odometer's only advances while the
    // bike is moving. Anything else - riding off, getting traction, a crash, a
    // pause longer than the odometer's window - ENDS the run rather than
    // pausing it, the same rule roost seconds follow. Reported only when the
    // run crosses a whole second, so this 100Hz path evaluates the catalogue at
    // most once a second and only while someone is sat there doing it.
    const bool digging =
        !isCrashed && rearWheelSpeedMs >= 0.0f && speedMs < DIG_MAX_SPEED_MS &&
        (rearWheelSpeedMs - speedMs) / std::max(1.0f, speedMs) > DIG_SLIP;
    if (!digging) {
        m_hasLastDigTime = false;
        m_digSec = 0.0;
        m_digReportedSec = 0.0;
    } else {
        const auto now = odometerNow();
        if (m_hasLastDigTime) {
            const double dt = std::chrono::duration_cast<std::chrono::microseconds>(
                                  now - m_lastDigTime).count() / 1000000.0;
            if (dt > 0.0 && dt <= 0.5) {
                m_digSec += dt;
                const double whole = std::floor(m_digSec);
                if (whole > m_digReportedSec) {
                    m_digReportedSec = whole;
                    m_exploration.onDiggingTime(whole);
                }
            } else if (dt > 0.5) {
                m_digSec = 0.0;              // a stall or a menu, not ten seconds of it
                m_digReportedSec = 0.0;
            }
        }
        m_lastDigTime = now;
        m_hasLastDigTime = true;
    }

    // Gear shift edge detection — count any gear change (including neutral transitions)
    if (m_lastGear >= 0 && currentGear >= 0 && currentGear != m_lastGear && stats) {
        stats->gearShiftCount++;
        if (!m_globalTotalsDirty) ++m_cachedTotalGearShifts;
        m_sessionGearShifts++;
        m_curLapGearShifts++;
        m_dirty = true;
        AchievementManager::getInstance().onStatsChanged();
    }
    if (currentGear >= 0) {
        m_lastGear = currentGear;
    }

    if (!stats) return;

    // Fuel burnt, integrated from the tank level FALLING - the same shape as
    // the odometer below, and for the same reason: the game reports a level,
    // not a consumption. A level that rises is a refuel, so it only
    // re-references. A fall larger than the tank is a context change (a new
    // bike, a session reset), not a tick's worth of burn.
    if (std::isfinite(fuelLitres) && fuelLitres >= 0.0f) {
        if (m_hasLastFuel) {
            const float burnt = m_lastFuel - fuelLitres;
            if (burnt > 0.0f && (m_tankCapacityL <= 0.0f || burnt < m_tankCapacityL)) {
                m_unflushedFuelL += burnt;
            }
            // Long Walk Home: the EDGE of the tank reaching EMPTY, so it
            // fires once per emptying rather than on every tick spent dry.
            //
            // Zero means zero. This used to allow a fraction of the tank as
            // "near enough", on the theory that the level might never quite
            // reach 0.0 - but that was a guess, never checked, and it is the
            // kind of guess that quietly redefines the row: at 1% it made
            // running dry and finishing on fumes the same instant. The API
            // documents m_fFuel only as litres, and a consumption model
            // clamps at zero rather than floating just above it.
            //
            // The capacity guard stays and is doing different work: a max fuel
            // of 0 is what the game reports when consumption is OFF, and a
            // level of 0.0 beside it means "cannot be known", not "empty".
            // Without it every player with fuel off earns this on lap one.
            //
            // If the game turns out to floor the level above zero this row
            // simply never fires, and one tank run dry on track says so.
            if (m_tankCapacityL > 0.0f) {
                if (m_lastFuel > 0.0f && fuelLitres <= 0.0f) m_exploration.onRanDry();
            }
        }
        m_lastFuel = fuelLitres;
        m_hasLastFuel = true;
    }

    // Top speed (session + per-lap)
    if (speedMs > m_sessionTopSpeedMs) {
        m_sessionTopSpeedMs = speedMs;
    }
    if (speedMs > m_curLapTopSpeedMs) {
        m_curLapTopSpeedMs = speedMs;
    }
    if (speedMs > stats->topSpeedMs) {
        stats->topSpeedMs = speedMs;
        m_dirty = true;
    }

    // Distance (integrated from speed * deltaTime)
    if (!m_currentBikeName.empty()) {
        auto now = odometerNow();

        if (!m_hasLastOdometerUpdateTime) {
            m_lastOdometerUpdateTime = now;
            m_hasLastOdometerUpdateTime = true;
        } else {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastOdometerUpdateTime);
            float deltaTime = duration.count() / 1000000.0f;
            m_lastOdometerUpdateTime = now;

            if (deltaTime > 0.0f && deltaTime <= 0.5f && speedMs >= MIN_MOVEMENT_SPEED_MS) {
                // The exploration clock's "was this a second of RIDING?" answer,
                // on the same threshold the odometer uses so the two can never
                // disagree about what moving is. Latched rather than sampled:
                // the tick runs at 1Hz off the draw path and telemetry at 100Hz,
                // so asking for the instantaneous speed once a second would drop
                // a second for every corner apex and gate drop.
                m_movedSinceTick = true;
                float distanceMeters = speedMs * deltaTime;
                m_sessionTripDistance += distanceMeters;
                m_curLapDistance += distanceMeters;
                // One lookup for the tick: this runs at 100Hz and the key is a
                // std::string, so the hash is worth doing once.
                double& bikeOdometer = m_bikeOdometers[m_currentBikeName];
                bikeOdometer += distanceMeters;
                stats->totalDistanceM += distanceMeters;
                if (!m_globalTotalsDirty) {
                    m_cachedTotalOdometer += distanceMeters;
                    // BOTH HALVES, like recomputeGlobalTotals sets them: the name
                    // is what the Loyal row shows beside the number, so bumping
                    // the distance alone made the tab read another bike's name
                    // against this bike's total until the next lap rebuilt them.
                    // The name is compared before it is copied: once the bike in
                    // use IS the lifetime leader this branch is taken every tick,
                    // and the copy would be the only work in it.
                    if (bikeOdometer > m_cachedMaxBikeOdometer) {
                        m_cachedMaxBikeOdometer = bikeOdometer;
                        if (m_cachedMaxBikeName != m_currentBikeName) {
                            m_cachedMaxBikeName = m_currentBikeName;
                        }
                    }
                }
                m_unsavedDistance += distanceMeters;
                // Only mark dirty every ~100m to avoid per-frame save overhead.
                // The same mark is the achievement evaluation's cadence for the
                // continuous metrics (distance, ride time): never per tick.
                if (m_unsavedDistance >= 100.0) {
                    m_dirty = true;
                    m_unsavedDistance = 0.0;
                    // Carbon Footprint rides this cadence rather than its own:
                    // the evaluation below is the one it would have triggered.
                    if (m_unflushedFuelL > 0.0) {
                        m_exploration.onFuelBurnt(m_unflushedFuelL);
                        m_unflushedFuelL = 0.0;
                    }
                    AchievementManager::getInstance().onStatsChanged();
                }
            }
        }
    }
}
