// ============================================================================
// core/plugin_data_telemetry.cpp
// Vehicle / speedometer / ECU / suspension / input telemetry
// (extracted verbatim from plugin_data.cpp; no behavior change)
// ============================================================================

#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "hud_manager.h"  // Direct include for notification
#if GAME_HAS_DISCORD
#include "discord_manager.h"  // Direct include for Discord presence updates
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"  // Steam friends rich-presence integration
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"  // Direct include for web overlay updates
#endif
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void PluginData::updateDebugMetrics(float fps, float pluginTimeMs, float pluginPercent) {
    m_debugMetrics.currentFps = fps;
    m_debugMetrics.pluginTimeMs = pluginTimeMs;
    m_debugMetrics.pluginPercent = pluginPercent;

    // Notify HudManager that debug metrics changed
    notifyHudManager(DataChangeType::DebugMetrics);
}

void PluginData::updateSpeedometer(float speedometer, int gear, int rpm, float fuel) {
    m_bikeTelemetry.speedometer = speedometer;
    m_bikeTelemetry.gear = gear;
    m_bikeTelemetry.rpm = rpm;
    m_bikeTelemetry.fuel = fuel;
    m_bikeTelemetry.isValid = true;

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    // This saves ~200 deque operations/second at 100Hz physics rate
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        // Add RPM to history (normalize to 0-1 range using limiterRPM as max, clamp to non-negative)
        // Safety: Only normalize if limiterRPM is valid to avoid division by zero
        float normalizedRpm = 0.0f;
        if (m_sessionData.limiterRPM > 0) {
            normalizedRpm = static_cast<float>(std::max(0, rpm)) / static_cast<float>(m_sessionData.limiterRPM);
        }
        m_historyBuffers.addSample(m_historyBuffers.rpm, normalizedRpm);

        // Add gear to history (normalize to 0-1 range using numberOfGears as max)
        // Safety: Only normalize if numberOfGears is valid to avoid division by zero
        float normalizedGear = 0.0f;
        if (m_bikeTelemetry.numberOfGears > 0) {
            normalizedGear = static_cast<float>(std::max(0, gear)) / static_cast<float>(m_bikeTelemetry.numberOfGears);
        }
        m_historyBuffers.addSample(m_historyBuffers.gear, normalizedGear);
    }

    // Notify HudManager that telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::invalidateSpeedometer() {
    m_bikeTelemetry.isValid = false;
    // Notify HudManager so widgets can update to show placeholder
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateRoll(float roll) {
    m_bikeTelemetry.roll = roll;
    // No separate notification - roll updates at same frequency as speedometer
    // which already notifies with InputTelemetry
}

void PluginData::updatePitch(float pitch) {
    m_bikeTelemetry.pitch = pitch;
    // Same as updateRoll — updates at telemetry rate, no separate notification.
}

void PluginData::updateAcceleration(float accelX, float accelY, float accelZ) {
    m_bikeTelemetry.accelX = accelX;
    m_bikeTelemetry.accelY = accelY;
    m_bikeTelemetry.accelZ = accelZ;
    // No separate notification - acceleration updates at same frequency as roll/pitch
    // which are bundled with the InputTelemetry notification from updateSpeedometer().
}

void PluginData::updateTemperatures(float engineTemp, float waterTemp) {
    m_bikeTelemetry.engineTemperature = engineTemp;
    m_bikeTelemetry.waterTemperature = waterTemp;
    // No separate notification - temperatures update at same frequency as other telemetry
}

void PluginData::updateTreadTemperatures(const float temps[2][3]) {
    for (int w = 0; w < 2; w++) {
        for (int s = 0; s < 3; s++) {
            m_bikeTelemetry.treadTemperature[w][s] = temps[w][s];
        }
    }
    // No separate notification - temperatures update at same frequency as other telemetry
}

void PluginData::updateEcuData(int ecuMode, const char* engineMapping, int tractionControl, int engineBraking, int antiWheeling, int ecuState) {
    m_bikeTelemetry.ecuMode = ecuMode;
    if (engineMapping) {
        strncpy_s(m_bikeTelemetry.engineMapping, sizeof(m_bikeTelemetry.engineMapping), engineMapping, _TRUNCATE);
    } else {
        m_bikeTelemetry.engineMapping[0] = '\0';
    }
    m_bikeTelemetry.tractionControl = tractionControl;
    m_bikeTelemetry.engineBraking = engineBraking;
    m_bikeTelemetry.antiWheeling = antiWheeling;
    m_bikeTelemetry.ecuState = ecuState;
    // No separate notification - ECU values update at same frequency as other telemetry
}

void PluginData::updateSuspensionMaxTravel(float frontMaxTravel, float rearMaxTravel) {
    m_bikeTelemetry.frontSuspMaxTravel = frontMaxTravel;
    m_bikeTelemetry.rearSuspMaxTravel = rearMaxTravel;
    // No notification needed - max travel is set once during bike initialization
}

void PluginData::updateSuspensionLength(float frontLength, float rearLength) {
    m_bikeTelemetry.frontSuspLength = frontLength;
    m_bikeTelemetry.rearSuspLength = rearLength;

    // Calculate compression percentages and add to history
    // Compression = (maxTravel - currentLength) / maxTravel
    // 0% = fully extended, 100% = fully compressed
    float frontCompression = 0.0f;
    float rearCompression = 0.0f;

    if (m_bikeTelemetry.frontSuspMaxTravel > 0) {
        frontCompression = (m_bikeTelemetry.frontSuspMaxTravel - frontLength) / m_bikeTelemetry.frontSuspMaxTravel;
        frontCompression = std::max(0.0f, std::min(1.0f, frontCompression));  // Clamp to 0-1
    }

    if (m_bikeTelemetry.rearSuspMaxTravel > 0) {
        rearCompression = (m_bikeTelemetry.rearSuspMaxTravel - rearLength) / m_bikeTelemetry.rearSuspMaxTravel;
        rearCompression = std::max(0.0f, std::min(1.0f, rearCompression));  // Clamp to 0-1
    }

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        m_historyBuffers.addSample(m_historyBuffers.frontSusp, frontCompression);
        m_historyBuffers.addSample(m_historyBuffers.rearSusp, rearCompression);
    }

    // Notify HudManager that telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateInputTelemetry(float steer, float throttle, float frontBrake, float rearBrake, float clutch) {
    // Update telemetry data (processed bike inputs)
    m_inputTelemetry.steer = steer;
    m_inputTelemetry.throttle = throttle;
    m_inputTelemetry.frontBrake = frontBrake;
    m_inputTelemetry.rearBrake = rearBrake;
    m_inputTelemetry.clutch = clutch;

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        m_historyBuffers.addSample(m_historyBuffers.throttle, throttle);
        m_historyBuffers.addSample(m_historyBuffers.frontBrake, frontBrake);
        m_historyBuffers.addSample(m_historyBuffers.rearBrake, rearBrake);
        m_historyBuffers.addSample(m_historyBuffers.clutch, clutch);
        m_historyBuffers.addSample(m_historyBuffers.steer, steer);
    }

    // Notify HudManager that input telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateRaceVehicleTelemetry(float speedometer, int gear, int rpm, float throttle, float frontBrake, float lean) {
    // Update current values (for widgets that display latest value)
    m_bikeTelemetry.speedometer = speedometer;
    m_bikeTelemetry.gear = gear;
    m_bikeTelemetry.rpm = rpm;
    m_bikeTelemetry.roll = lean;  // Lean angle available in RaceVehicleData
    m_bikeTelemetry.isValid = true;

    m_inputTelemetry.throttle = throttle;
    m_inputTelemetry.frontBrake = frontBrake;

    // OPTIMIZATION: Only add to history buffers if TelemetryHud is visible
    // Only add to history for data that's actually available in SPluginsRaceVehicleData_t
    // Other buffers (rearBrake, clutch, steer, fuel, suspension) are not updated
    if (HudManager::getInstance().isTelemetryHistoryNeeded()) {
        float normalizedRpm = 0.0f;
        if (m_sessionData.limiterRPM > 0) {
            normalizedRpm = static_cast<float>(std::max(0, rpm)) / static_cast<float>(m_sessionData.limiterRPM);
        }
        m_historyBuffers.addSample(m_historyBuffers.rpm, normalizedRpm);

        float normalizedGear = 0.0f;
        if (m_bikeTelemetry.numberOfGears > 0) {
            normalizedGear = static_cast<float>(std::max(0, gear)) / static_cast<float>(m_bikeTelemetry.numberOfGears);
        }
        m_historyBuffers.addSample(m_historyBuffers.gear, normalizedGear);

        m_historyBuffers.addSample(m_historyBuffers.throttle, throttle);
        m_historyBuffers.addSample(m_historyBuffers.frontBrake, frontBrake);
    }

    // Notify HudManager that telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}

void PluginData::updateXInputData(const XInputData& xinputData) {
    // Update XInput data (raw controller inputs)
    m_inputTelemetry.leftStickX = xinputData.leftStickX;
    m_inputTelemetry.leftStickY = xinputData.leftStickY;
    m_inputTelemetry.rightStickX = xinputData.rightStickX;
    m_inputTelemetry.rightStickY = xinputData.rightStickY;
    m_inputTelemetry.leftTrigger = xinputData.leftTrigger;
    m_inputTelemetry.rightTrigger = xinputData.rightTrigger;
    m_inputTelemetry.xinputConnected = xinputData.isConnected;

    // Add both sticks to history
    m_historyBuffers.addStickSample(m_historyBuffers.leftStick, xinputData.leftStickX, xinputData.leftStickY);
    m_historyBuffers.addStickSample(m_historyBuffers.rightStick, xinputData.rightStickX, xinputData.rightStickY);

    // Notify HudManager that input telemetry changed
    notifyHudManager(DataChangeType::InputTelemetry);
}
