// ============================================================================
// core/xinput_reader.h
// XInput controller reader for raw gamepad input access
// ============================================================================
#pragma once

#include <windows.h>
#include <Xinput.h>

// XInput controller state data
struct XInputData {
    // Left stick (typically steering/throttle)
    float leftStickX;       // -1.0 to 1.0 (left to right)
    float leftStickY;       // -1.0 to 1.0 (down to up)

    // Right stick (rider body lean)
    float rightStickX;      // -1.0 to 1.0 (lean left to right)
    float rightStickY;      // -1.0 to 1.0 (lean back to forward)

    // Triggers
    float leftTrigger;      // 0.0 to 1.0 (rear brake typically)
    float rightTrigger;     // 0.0 to 1.0 (front brake typically)

    // D-Pad
    bool dpadUp;
    bool dpadDown;
    bool dpadLeft;
    bool dpadRight;

    // Buttons
    bool buttonA;
    bool buttonB;
    bool buttonX;
    bool buttonY;
    bool leftShoulder;
    bool rightShoulder;
    bool leftThumb;         // Left stick click
    bool rightThumb;        // Right stick click
    bool buttonStart;
    bool buttonBack;

    // Connection state
    bool isConnected;

    XInputData() : leftStickX(0.0f), leftStickY(0.0f),
                   rightStickX(0.0f), rightStickY(0.0f),
                   leftTrigger(0.0f), rightTrigger(0.0f),
                   dpadUp(false), dpadDown(false), dpadLeft(false), dpadRight(false),
                   buttonA(false), buttonB(false), buttonX(false), buttonY(false),
                   leftShoulder(false), rightShoulder(false),
                   leftThumb(false), rightThumb(false),
                   buttonStart(false), buttonBack(false),
                   isConnected(false) {}
};

class XInputReader {
public:
    static XInputReader& getInstance();

    // Update controller state (call once per frame)
    void update();

    // Get current controller data
    const XInputData& getData() const { return m_data; }

    // Set which controller to read (0-3)
    void setControllerIndex(int index);
    int getControllerIndex() const { return m_controllerIndex; }

private:
    XInputReader();
    ~XInputReader() = default;
    XInputReader(const XInputReader&) = delete;
    XInputReader& operator=(const XInputReader&) = delete;

    // Normalize stick values with deadzone
    float normalizeStickValue(SHORT value, SHORT deadzone) const;

    // Normalize trigger value
    float normalizeTriggerValue(BYTE value) const;

    static constexpr SHORT STICK_DEADZONE = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    static constexpr BYTE TRIGGER_THRESHOLD = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

    XInputData m_data;
    int m_controllerIndex;
};
