// ============================================================================
// hud/prestige_widget.h
// The prestige badge: a piece of artwork on a panel, and the only thing in the
// plugin that cannot be switched on from the settings menu.
//
// It is what StatsManager::prestige() buys. Every other widget is a readout --
// it tells you something about the session you are in. This one tells everyone
// watching that the player finished the whole achievement ladder and then gave
// it back, which is the entire point of wearing it.
//
// THE ARTWORK IS THE WIDGET, exactly as the dial is the Radar's: it declares a
// texture stem and m_textureRequired, so the Texture column cycles
// textures/prestige_widget_N.tga through the ordinary variant machinery and
// there is no bespoke picker to keep in step. ONE badge ships (the crown);
// adding another is dropping the next numbered file in that folder, and the
// column grows by itself.
//
// LOCKED MEANS INVISIBLE, NOT HIDDEN: with no prestige level the widget draws
// nothing and clears its bounds (so it cannot be dragged or hit-tested), and
// the Widgets tab leaves its row out entirely. That is one condition asked in
// two places rather than a stored flag that could disagree with the stats file
// -- a hand-edited file that zeroes `prestige` takes the badge off, which is
// the honest outcome. Developer mode unlocks it too, so the widget can be laid
// out and its art checked without earning the catalogue first.
// ============================================================================
#pragma once

#include "base_hud.h"

class PrestigeWidget : public BaseHud {
    friend class SettingsHud;
    friend class SettingsManager;

public:
    PrestigeWidget();
    virtual ~PrestigeWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "crown"; }
    void resetToDefaults();

    // Is the badge wearable -- a prestige level taken, or developer mode on.
    // Asked here so the widget and its settings row read one function.
    static bool isUnlocked();

private:
    void rebuildRenderData() override;
    void rebuildLayout() override;

    // What the last rebuild was built for -- see update(). Not a setting.
    bool m_builtUnlocked = false;
};
