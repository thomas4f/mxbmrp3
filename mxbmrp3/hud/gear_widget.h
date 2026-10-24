// ============================================================================
// hud/gear_widget.h
// Gear widget - displays current gear with shift/limiter indicators
// ============================================================================
#pragma once

#include "base_hud.h"
#include "digit_roll.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class GearWidget : public BaseHud {
public:
    GearWidget();
    virtual ~GearWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // INI-only settings
    bool m_bShowShiftColor = true;     // Red gear text at shift RPM
    bool m_bShowLimiterCircle = true;  // Circle indicator at limiter RPM

protected:
    void rebuildLayout() override;

private:
    // No per-quad index tracking: rebuildLayout defers to rebuildRenderData (the
    // box-model plan is the one source of geometry), so nothing repositions the
    // limiter circle in place anymore.
    void rebuildRenderData() override;

    int m_circleSprite = 0;  // "gear_circle", resolved once per rebuild

    // Digit roll (Motion, digit_roll.h), and whose gear it last showed.
    DigitRoll::Roller m_gearRoll;
    int m_rollRaceNum = -1;
};
