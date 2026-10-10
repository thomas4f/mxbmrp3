// ============================================================================
// hud/settings_button_widget.h
// Settings button widget - draggable button to toggle settings menu
// Shows a menu icon when settings closed, a close icon when settings open
// ("[=]" / "[x]" with UI icons off)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"

class SettingsButtonWidget : public BaseHud {
    friend class SettingsManager;

public:
    SettingsButtonWidget();
    virtual ~SettingsButtonWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Check if the button is being clicked
    bool isClicked() const;

private:
    void rebuildRenderData() override;

    // Text stand-ins for the menu/close glyphs while UI icons are off
    static constexpr const char* TEXT_CLOSED = "[=]";
    static constexpr const char* TEXT_OPEN = "[x]";
};
