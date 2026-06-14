// ============================================================================
// handlers/run_telemetry_handler.cpp
// Processes run telemetry data (input, controller, vehicle telemetry)
// ============================================================================
#include "run_telemetry_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/xinput_reader.h"
#include "../core/stats_manager.h"
#include "../core/fmx_manager.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>

DEFINE_HANDLER_SINGLETON(RunTelemetryHandler)

void RunTelemetryHandler::handleRunTelemetry(Unified::TelemetryData* psTelemetryData) {
    // Update input telemetry data
    if (psTelemetryData) {
        // Update speedometer, gear, RPM, and fuel
        PluginData::getInstance().updateSpeedometer(psTelemetryData->speedometer, psTelemetryData->gear, psTelemetryData->rpm, psTelemetryData->fuel);

        // Update stats: distance, top speed, crash detection
        {
            const TrackPositionData* playerPos = PluginData::getInstance().getPlayerTrackPosition();
            bool isCrashed = playerPos && playerPos->crashed;
            StatsManager::getInstance().updateTelemetry(psTelemetryData->speedometer, isCrashed, psTelemetryData->gear);
        }

        // Update input telemetry data (bike-specific uses front/rear brake)
        float frontBrake = psTelemetryData->brake;
        float rearBrake = 0.0f;
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            frontBrake = psTelemetryData->bike.frontBrake;
            rearBrake = psTelemetryData->bike.rearBrake;
        }
        PluginData::getInstance().updateInputTelemetry(
            psTelemetryData->steer,
            psTelemetryData->throttle,
            frontBrake,
            rearBrake,
            psTelemetryData->clutch
        );

        // Update suspension telemetry data (bike-specific)
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            PluginData::getInstance().updateSuspensionLength(
                psTelemetryData->bike.suspLength[0],  // Front suspension current length
                psTelemetryData->bike.suspLength[1]   // Rear suspension current length
            );
        }

        // Update roll/pitch (lean + nose-up/down angle)
        PluginData::getInstance().updateRoll(psTelemetryData->roll);
        PluginData::getInstance().updatePitch(psTelemetryData->pitch);

        // Update G-forces (chassis-local, already in g units, averaged over 10ms by the engine)
        PluginData::getInstance().updateAcceleration(
            psTelemetryData->accelX,
            psTelemetryData->accelY,
            psTelemetryData->accelZ
        );

#if GAME_HAS_FMX
        // Update FMX trick detection (bikes only - assumes 2 wheels, lean angles)
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            FmxManager::getInstance().updateFromTelemetry(*psTelemetryData);
        }
#endif

        // Update engine and water temperatures
        PluginData::getInstance().updateTemperatures(
            psTelemetryData->engineTemperature,
            psTelemetryData->waterTemperature
        );

        // Update tyre tread temperatures (GP Bikes bike-specific)
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            PluginData::getInstance().updateTreadTemperatures(psTelemetryData->bike.treadTemperature);
        }

#if GAME_HAS_ECU
        // Update ECU / electronic rider aids (GP Bikes bike-specific)
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            PluginData::getInstance().updateEcuData(
                psTelemetryData->bike.ecuMode,
                psTelemetryData->bike.engineMapping,
                psTelemetryData->bike.tractionControl,
                psTelemetryData->bike.engineBraking,
                psTelemetryData->bike.antiWheeling,
                psTelemetryData->bike.ecuState
            );
        }
#endif

        // Controller rumble based on suspension and wheel slip (bike-specific)
        // Front/rear are kept separate so the Bumps effect can be split; the engine
        // collapses them to max when the effect isn't split.
        float suspVelFront = 0.0f;
        float suspVelRear = 0.0f;
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            // Suspension velocity: negative = compression, we negate so compression is positive
            suspVelFront = -psTelemetryData->bike.suspVelocity[0];
            suspVelRear = -psTelemetryData->bike.suspVelocity[1];
        }

        // Check wheel contact (wheelMaterial: 0 = not in contact)
        bool frontWheelContact = psTelemetryData->wheelMaterial[0] != 0;
        bool rearWheelContact = psTelemetryData->wheelMaterial[1] != 0;

        // Wheel slip calculations
        float vehicleSpeed = psTelemetryData->speedometer;
        float frontWheelSpeed = psTelemetryData->wheelSpeed[0];
        float rearWheelSpeed = psTelemetryData->wheelSpeed[1];

        float wheelOverrun = 0.0f;
        // Front/rear underrun kept separate so the Lockup effect can be split; the
        // engine collapses them to max when the effect isn't split.
        float frontUnderrun = 0.0f;
        float rearUnderrun = 0.0f;

        // Use minimum 1 m/s for ratio denominator to allow burnout detection at low speeds
        // while preventing division by zero
        float speedForRatio = std::max(1.0f, vehicleSpeed);

        // Wheelspin: rear wheel overrun (only when rear wheel is in contact)
        // Works at any speed - burnouts at standstill will trigger strong feedback
        if (rearWheelContact && rearWheelSpeed > vehicleSpeed) {
            wheelOverrun = (rearWheelSpeed - vehicleSpeed) / speedForRatio;
        }

        // Brake lockup: wheel underrun (only for wheels in contact with ground)
        // Requires some vehicle speed - can't lock up wheels when stationary
        // Also requires positive wheel speed - when rolling backwards, wheel speed is negative
        // but speedometer shows positive (absolute) speed, causing false lockup detection
        if (vehicleSpeed > 1.0f) {
            if (frontWheelContact && frontWheelSpeed >= 0.0f && frontWheelSpeed < vehicleSpeed) {
                frontUnderrun = (vehicleSpeed - frontWheelSpeed) / vehicleSpeed;
            }
            if (rearWheelContact && rearWheelSpeed >= 0.0f && rearWheelSpeed < vehicleSpeed) {
                rearUnderrun = (vehicleSpeed - rearWheelSpeed) / vehicleSpeed;
            }
        }

        // Get RPM for engine vibration effect
        float rpm = static_cast<float>(psTelemetryData->rpm);

        // Calculate slip angle using horizontal velocity (X,Z plane) and yaw
        // Based on map coordinate convention: X=east-west, Y=altitude, Z=north-south
        // Yaw = angle from north, so forward direction = (sin(yaw), cos(yaw)) in X,Z plane
        float lateralG = 0.0f;
        float speed = psTelemetryData->speedometer;
        if (speed > 2.0f) {
            using namespace PluginConstants::Math;
            float yawRad = psTelemetryData->yaw * DEG_TO_RAD;
            float sinYaw = std::sin(yawRad);
            float cosYaw = std::cos(yawRad);

            // Project horizontal velocity onto forward and lateral directions
            float vx = psTelemetryData->velocityX;
            float vz = psTelemetryData->velocityZ;
            float forwardVel = vx * sinYaw + vz * cosYaw;
            float lateralVel = vx * cosYaw - vz * sinYaw;

            // Slip angle in degrees
            lateralG = std::atan2(std::abs(lateralVel), std::abs(forwardVel)) * RAD_TO_DEG;
        }

        // Airborne detection: both wheels off the ground
        bool isAirborne = !frontWheelContact && !rearWheelContact;

        // Surface roughness: speed when on rough/off-track surfaces
        // Material 0 = no contact, 1 = main track (tarmac), >1 = grass/dirt/gravel
        // Rumble is proportional to speed when on rough surface
        float surfaceSpeed = 0.0f;
        int frontMaterial = psTelemetryData->wheelMaterial[0];
        int rearMaterial = psTelemetryData->wheelMaterial[1];
        // Trigger if either wheel is on rough surface (material > 1)
        if (frontMaterial > 1 || rearMaterial > 1) {
            surfaceSpeed = vehicleSpeed;
        }

        // Steer torque for handlebar resistance feedback (bike-specific)
        float steerTorque = 0.0f;
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike) {
            steerTorque = psTelemetryData->bike.steerTorque;
        }

        // Wheelie detection: front wheel off ground, rear wheel on ground, bike tilted back
        // Pitch is negative when tilted back (wheelie direction)
        float wheelieIntensity = 0.0f;
        if (!frontWheelContact && rearWheelContact && psTelemetryData->pitch < 0.0f) {
            // Use absolute pitch angle as intensity (in degrees)
            wheelieIntensity = std::abs(psTelemetryData->pitch);
        }

        // Rev limiter: RPM as a percentage of the bike's real limiter RPM (auto per-bike).
        // Throttle-gated so engine-braking / downshift blips near redline don't false-fire.
        float revLimiterPct = 0.0f;
        int limiterRPM = PluginData::getInstance().getSessionData().limiterRPM;
        if (limiterRPM > 0 && psTelemetryData->throttle > 0.1f) {
            revLimiterPct = 100.0f * rpm / static_cast<float>(limiterRPM);
        }

        // Pit limiter: binary flag (GP Bikes reports it; 0 on games that don't)
        float pitLimiterActive = 0.0f;
        if (psTelemetryData->vehicleType == Unified::VehicleType::Bike && psTelemetryData->bike.pitLimiter) {
            pitLimiterActive = 1.0f;
        }

        // Check if player is crashed and rumble should be suppressed (but still update graph)
        bool suppressRumble = false;
        const RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
        if (!rumbleConfig.rumbleWhenCrashed) {
            const TrackPositionData* playerPos = PluginData::getInstance().getPlayerTrackPosition();
            if (playerPos && playerPos->crashed) {
                suppressRumble = true;
            }
        }

        XInputReader::getInstance().updateRumbleFromTelemetry(suspVelFront, suspVelRear, wheelOverrun, frontUnderrun, rearUnderrun, rpm, lateralG, surfaceSpeed, steerTorque, wheelieIntensity, revLimiterPct, pitLimiterActive, isAirborne, suppressRumble);
    } else {
        // No telemetry data available (e.g., spectating retired rider, menu, etc.)
        PluginData::getInstance().invalidateSpeedometer();
        XInputReader::getInstance().stopVibration();
    }

    // Update XInput controller state (same rate as telemetry)
    XInputReader::getInstance().update();
    PluginData::getInstance().updateXInputData(XInputReader::getInstance().getData());
}
