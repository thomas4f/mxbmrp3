// ============================================================================
// hud/settings_button_widget.h
// Settings button widget - draggable button to toggle settings menu
// Shows "[=]" when settings closed, "[x]" when settings open
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

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Cached state to detect changes
    bool m_bCachedSettingsVisible;

    // Display constants
    static constexpr int BUTTON_WIDTH_CHARS = 3;  // "[=]" or "[x]"
    static constexpr const char* TEXT_CLOSED = "[=]";
    static constexpr const char* TEXT_OPEN = "[x]";
};
