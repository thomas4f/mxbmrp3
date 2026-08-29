// ============================================================================
// hud/speedo_widget.h
// Speedo widget - displays rotating needle (0-230 km/h) with dial background
// ============================================================================
#pragma once

#include <string>

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../core/asset_manager.h"

class SpeedoWidget : public BaseHud {
public:
    SpeedoWidget();
    virtual ~SpeedoWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // THE SELECTED GAUGES PACK, BY NAME -- see TachoWidget::getGaugesPack for the
    // rule and why the two widgets each hold their own.
    const std::string& getGaugesPack() const { return m_gaugesPack; }
    void setGaugesPack(const std::string& name);
    const GaugesAsset* activePack() const;

    // Needle color (configurable via INI). Stored vs effective -- see
    // TachoWidget::setNeedleColor for why there are two.
    void setNeedleColor(unsigned long color) {
        m_needleColor = color;
        m_needleColorSet = true;
        setDataDirty();
    }
    unsigned long getNeedleColor() const { return m_needleColor; }
    bool hasNeedleColorOverride() const { return m_needleColorSet; }
    // Back to "whatever the pack says". The settings file spells this `pack`.
    void clearNeedleColorOverride() {
        m_needleColorSet = false;
        m_needleColor = DEFAULT_NEEDLE_COLOR;
        setDataDirty();
    }
    unsigned long effectiveNeedleColor() const;
    static constexpr unsigned long DEFAULT_NEEDLE_COLOR = PluginUtils::makeColor(255, 0, 0);  // Red

    // Odometer/Trip meter visibility (configurable via INI)
    void setShowOdometer(bool show) { m_showOdometer = show; setDataDirty(); }
    bool getShowOdometer() const { return m_showOdometer; }
    void setShowTripmeter(bool show) { m_showTripmeter = show; setDataDirty(); }
    bool getShowTripmeter() const { return m_showTripmeter; }

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // The face's range and sweep come from the pack now -- see
    // hud/gauge_geometry.h. The compiled 230 km/h these replace is also why this
    // mattered beyond modding: the same face ships to GP Bikes, where the needle
    // pinned on the straight.
    //
    // DIAL_SIZE stays: it is the widget's size on screen, not the face's scale.
    static constexpr float DIAL_SIZE = 0.15f;        // Base dial size in normalized coordinates

    // Needle smoothing (simulates physical inertia of analog gauge)
    static constexpr float NEEDLE_SMOOTH_FACTOR = 0.15f;  // 0.0-1.0: lower = smoother, higher = faster response
    float m_smoothedSpeed = 0.0f;  // Current smoothed speed value for needle display

    // Needle appearance. See TachoWidget for what the flag separates.
    unsigned long m_needleColor = DEFAULT_NEEDLE_COLOR;
    bool m_needleColorSet = false;

    std::string m_gaugesPack;

    // Odometer/Trip meter visibility
    bool m_showOdometer = true;   // Default: ON
    bool m_showTripmeter = false; // Default: OFF
};
