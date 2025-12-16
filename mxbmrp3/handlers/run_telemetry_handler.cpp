// ============================================================================
// handlers/run_telemetry_handler.cpp
// Processes run telemetry data (input, controller, bike telemetry)
// ============================================================================
#include "run_telemetry_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/xinput_reader.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>

DEFINE_HANDLER_SINGLETON(RunTelemetryHandler)

void RunTelemetryHandler::handleRunTelemetry(SPluginsBikeData_t* psBikeData, float fTime, float fPos) {
    // Update input telemetry data
    if (psBikeData) {
        // Update speedometer, gear, RPM, and fuel
        PluginData::getInstance().updateSpeedometer(psBikeData->m_fSpeedometer, psBikeData->m_iGear, psBikeData->m_iRPM, psBikeData->m_fFuel);

        // Update input telemetry data
        PluginData::getInstance().updateInputTelemetry(
            psBikeData->m_fSteer,
            psBikeData->m_fThrottle,
            psBikeData->m_fFrontBrake,
            psBikeData->m_fRearBrake,
            psBikeData->m_fClutch
        );

        // Update suspension telemetry data
        PluginData::getInstance().updateSuspensionLength(
            psBikeData->m_afSuspLength[0],  // Front suspension current length
            psBikeData->m_afSuspLength[1]   // Rear suspension current length
        );

        // Controller rumble based on suspension and wheel slip
        // Suspension velocity: negative = compression, we want max compression rate
        // m_afSuspVelocity[0] = front, [1] = rear
        float frontSuspVel = -psBikeData->m_afSuspVelocity[0];  // Negate so compression is positive
        float rearSuspVel = -psBikeData->m_afSuspVelocity[1];
        float suspensionVelocity = std::max(frontSuspVel, rearSuspVel);

        // Check wheel contact (m_aiWheelMaterial: 0 = not in contact)
        bool frontWheelContact = psBikeData->m_aiWheelMaterial[0] != 0;
        bool rearWheelContact = psBikeData->m_aiWheelMaterial[1] != 0;

        // Wheel slip calculations
        float vehicleSpeed = psBikeData->m_fSpeedometer;
        float frontWheelSpeed = psBikeData->m_afWheelSpeed[0];
        float rearWheelSpeed = psBikeData->m_afWheelSpeed[1];

        float wheelOverrun = 0.0f;
        float wheelUnderrun = 0.0f;

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
        if (vehicleSpeed > 1.0f) {
            float frontUnderrun = 0.0f;
            float rearUnderrun = 0.0f;
            if (frontWheelContact && frontWheelSpeed < vehicleSpeed) {
                frontUnderrun = (vehicleSpeed - frontWheelSpeed) / vehicleSpeed;
            }
            if (rearWheelContact && rearWheelSpeed < vehicleSpeed) {
                rearUnderrun = (vehicleSpeed - rearWheelSpeed) / vehicleSpeed;
            }
            wheelUnderrun = std::max(frontUnderrun, rearUnderrun);
        }

        // Get RPM for engine vibration effect
        float rpm = static_cast<float>(psBikeData->m_iRPM);

        // Calculate slip angle using horizontal velocity (X,Z plane) and yaw
        // Based on map coordinate convention: X=east-west, Y=altitude, Z=north-south
        // Yaw = angle from north, so forward direction = (sin(yaw), cos(yaw)) in X,Z plane
        float lateralG = 0.0f;
        float speed = psBikeData->m_fSpeedometer;
        if (speed > 2.0f) {
            constexpr float DEG_TO_RAD = 3.14159265358979f / 180.0f;
            constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979f;
            float yawRad = psBikeData->m_fYaw * DEG_TO_RAD;
            float sinYaw = std::sin(yawRad);
            float cosYaw = std::cos(yawRad);

            // Project horizontal velocity onto forward and lateral directions
            float vx = psBikeData->m_fVelocityX;
            float vz = psBikeData->m_fVelocityZ;
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
        int frontMaterial = psBikeData->m_aiWheelMaterial[0];
        int rearMaterial = psBikeData->m_aiWheelMaterial[1];
        // Trigger if either wheel is on rough surface (material > 1)
        if (frontMaterial > 1 || rearMaterial > 1) {
            surfaceSpeed = vehicleSpeed;
        }

        // Steer torque for handlebar resistance feedback
        float steerTorque = psBikeData->m_fSteerTorque;

        // Wheelie detection: front wheel off ground, rear wheel on ground, bike tilted back
        // Pitch is negative when tilted back (wheelie direction)
        float wheelieIntensity = 0.0f;
        if (!frontWheelContact && rearWheelContact && psBikeData->m_fPitch < 0.0f) {
            // Use absolute pitch angle as intensity (in degrees)
            wheelieIntensity = std::abs(psBikeData->m_fPitch);
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

        XInputReader::getInstance().updateRumbleFromTelemetry(suspensionVelocity, wheelOverrun, wheelUnderrun, rpm, lateralG, surfaceSpeed, steerTorque, wheelieIntensity, isAirborne, suppressRumble);
    } else {
        // No telemetry data available (e.g., spectating retired rider, menu, etc.)
        PluginData::getInstance().invalidateSpeedometer();
        XInputReader::getInstance().stopVibration();
    }

    // Update XInput controller state (same rate as telemetry)
    XInputReader::getInstance().update();
    PluginData::getInstance().updateXInputData(XInputReader::getInstance().getData());

    // Additional telemetry data available:
    // psBikeData contains: RPM, gear, speed, position, velocity, acceleration, suspension, etc.
    // fTime: on-track time in seconds
    // fPos: position on centerline (0.0 to 1.0)
}
