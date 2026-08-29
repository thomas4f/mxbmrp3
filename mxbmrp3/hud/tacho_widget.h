// ============================================================================
// hud/tacho_widget.h
// Tacho widget - displays rotating needle (0-15000 RPM) with dial background
// ============================================================================
#pragma once

#include <string>

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../core/asset_manager.h"

class TachoWidget : public BaseHud {
public:
    TachoWidget();
    virtual ~TachoWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // THE SELECTED GAUGES PACK, BY NAME (see AssetManager's GaugesAsset). By name
    // and not by index for the reason every other pack type is: an index into
    // discovery order reassigns every user's choice when a pack is added or
    // renamed. An unknown name degrades to the shipped default WITHOUT rewriting
    // what is stored, so putting the folder back restores the choice.
    //
    // Per widget rather than one setting for the pair: that is what the other two
    // pack HUDs do, and it makes "my tacho over the shipped speedo" a matter of
    // picking, with `base =` reserved for actually sharing artwork.
    const std::string& getGaugesPack() const { return m_gaugesPack; }
    void setGaugesPack(const std::string& name);

    // The pack actually in use once the name is resolved; nullptr only when no
    // gauges packs are installed at all.
    const GaugesAsset* activePack() const;

    // Needle color (configurable via INI).
    //
    // TWO ANSWERS, deliberately. The STORED value is what the settings file holds
    // and is what gets saved; the EFFECTIVE one is what the needle is drawn in,
    // and defers to the pack until the user actually states a colour. A pack has
    // to be able to ship a needle that contrasts with its own face (see
    // gauge_geometry.h), and it cannot do that if a factory-default red is
    // indistinguishable from a red somebody chose.
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

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // What the FACE reads, and where its ends sit, now come from the pack -- see
    // hud/gauge_geometry.h for why they had to: they used to be MIN_RPM/MAX_RPM
    // and MIN/MAX_ANGLE_DEG right here, so any dial art but the shipped one got a
    // needle that lied.
    //
    // DIAL_SIZE stays: it is the widget's own base size on screen, scaled by the
    // user's slider, and says nothing about what the face is printed with.
    static constexpr float DIAL_SIZE = 0.15f;         // Base dial size in normalized coordinates

    // Needle smoothing (simulates physical inertia of analog gauge)
    static constexpr float NEEDLE_SMOOTH_FACTOR = 0.15f;  // 0.0-1.0: lower = smoother, higher = faster response
    float m_smoothedRpm = 0.0f;  // Current smoothed RPM value for needle display

    // Needle appearance. See setNeedleColor: the flag is what separates "the user
    // picked red" from "nobody has picked anything and red is the factory value".
    unsigned long m_needleColor = DEFAULT_NEEDLE_COLOR;
    bool m_needleColorSet = false;

    std::string m_gaugesPack;
};
