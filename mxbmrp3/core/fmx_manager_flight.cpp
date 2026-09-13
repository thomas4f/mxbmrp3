// ============================================================================
// core/fmx_manager_flight.cpp
// Flight measurement — every jump, trick or not. Split out of
// fmx_manager_detection.cpp when that file reached its length budget; the
// FmxManager class, members and API are unchanged, only where these three
// method bodies live. It is a natural seam: this block is deliberately
// independent of trick classification (see below), sharing only the ground
// state and the frame's telemetry.
// ============================================================================
#include "fmx_manager.h"
#include "fmx_manager_internal.h"
#include "stats_manager.h"
#include "../diagnostics/logger.h"

#include <cmath>

// ============================================================================
// Flight tracking - every jump, trick or not
// ============================================================================
// WHY THIS IS NOT PART OF TRICK DETECTION. A trick has to be classified, has to
// commit, and has to be LANDED before it counts; most jumps on an MX lap are
// none of those, and Air Miles used to throw all of them away. This measures
// the flight itself, so a plain double kicker counts the same as one you
// whipped. The three numbers it feeds are the same measurement read three ways:
// how long, how high above where you left the ground, and how far across it.
//
// THE LANDING DECIDES WHICH HALF, not whether. The three TOTALS are banked at
// touchdown -- the flight happened and was measured, so it counts whether or
// not the rider stays upright after it. The three MAXIMA wait out the settle
// window first (creditFlight), because a personal best has to be ridden away
// from. A tumble that never touches down credits nothing either way:
// abortFlight() throws it out, since a bike falling is not flying.
namespace {
// Below this a "flight" is wheel chatter over braking bumps, not a jump: both
// wheels leave the ground for a few tens of milliseconds constantly on rough
// ground, and counting that as airtime would make a lap of chop look like a
// jump session.
constexpr float FLIGHT_MIN_SEC = 0.25f;
// Sanity ceilings. A tumble down a hillside after a crash, or a physics glitch,
// must not become a lifetime record nobody can beat. Generous enough that no
// real jump on any track comes near them.
constexpr float FLIGHT_MAX_HEIGHT_M = 60.0f;
constexpr float FLIGHT_MAX_DISTANCE_M = 250.0f;
}  // namespace

void FmxManager::abortFlight() {
    m_inFlight = false;
    m_flightSec = 0.0f;
    m_flightPending = false;
    m_flightSettleSec = 0.0f;
}

// Ridden away from: the maxima. The totals were banked at touchdown.
void FmxManager::creditFlight() {
    m_flightPending = false;
    m_flightSettleSec = 0.0f;
    FMX_LOG("FMX: flight ridden away %.2fs, %.1fm up, %.1fm across",
        m_pendingFlightSec, m_pendingHeightM, m_pendingDistanceM);
    StatsManager::getInstance().exploration().onFlightLanded(
        m_pendingFlightSec, m_pendingHeightM, m_pendingDistanceM);
}

void FmxManager::updateFlight(const Unified::TelemetryData& telemetry, float dt) {
    // A landed flight waiting out its settle window. The countdown runs whether
    // or not the bike is back in the air, so a rhythm section credits each jump
    // as it goes instead of holding one hostage to the next one's landing; a
    // crash or a teleport clears it through abortFlight().
    if (m_flightPending) {
        m_flightSettleSec += dt;
        if (m_flightSettleSec >= m_config.landingGracePeriod) creditFlight();
    }

    const bool airborne = m_groundState.isAirborne();

    if (airborne) {
        if (!m_inFlight) {
            m_inFlight = true;
            m_flightSec = 0.0f;
            m_flightTakeoffX = telemetry.posX;
            m_flightTakeoffY = telemetry.posY;
            m_flightTakeoffZ = telemetry.posZ;
            m_flightPeakY = telemetry.posY;
        }
        m_flightSec += dt;
        if (telemetry.posY > m_flightPeakY) m_flightPeakY = telemetry.posY;
        return;
    }

    if (!m_inFlight) return;
    m_inFlight = false;
    const float seconds = m_flightSec;
    m_flightSec = 0.0f;
    if (seconds < FLIGHT_MIN_SEC) return;

    const float height = m_flightPeakY - m_flightTakeoffY;
    const float dx = telemetry.posX - m_flightTakeoffX;
    const float dz = telemetry.posZ - m_flightTakeoffZ;
    const float distance = std::sqrt(dx * dx + dz * dz);
    const bool sane = std::isfinite(height) && std::isfinite(distance) &&
                      height >= 0.0f && height < FLIGHT_MAX_HEIGHT_M &&
                      distance >= 0.0f && distance < FLIGHT_MAX_DISTANCE_M;
    if (!sane) return;

    // A flight still waiting out its window when the NEXT one lands has been
    // ridden away from - the rider jumped again - so bank it before the slot is
    // reused. Without this a rhythm section silently ate every jump but the
    // last: those flights land 0.25-0.75s apart, which is inside the window,
    // so each touchdown overwrote a pending set that had never been credited.
    if (m_flightPending) creditFlight();

    // Touchdown: the TOTALS are banked here, and here only. Casing one out
    // still counts toward them - you were in the air, and the sums say how
    // much air - which is what makes them differ from the maxima below.
    FMX_LOG("FMX: flight down %.2fs, %.1fm up, %.1fm across", seconds, height, distance);
    StatsManager::getInstance().exploration().onFlight(seconds, height, distance);

    // ...and the MAXIMA are parked until the rider has stayed upright for the
    // settle window above: a best you case out of is not a best you made.
    m_flightPending = true;
    m_flightSettleSec = 0.0f;
    m_pendingFlightSec = seconds;
    m_pendingHeightM = height;
    m_pendingDistanceM = distance;
}
