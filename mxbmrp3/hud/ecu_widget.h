// ============================================================================
// hud/ecu_widget.h
// ECU Widget - displays GP Bikes electronic rider aids as a 2x2 grid of chips
// GP Bikes only - engine mapping, traction control, engine braking, anti-wheeling.
// Chip brightness tracks live intervention (ecuState); an underline marks the
// page the rider is currently adjusting (ecuMode).
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../game/game_config.h"

#if GAME_HAS_ECU

class EcuWidget : public BaseHud {
public:
    // Per-chip visibility flags (bitfield) - configurable via INI / settings tab
    enum RowFlags : uint32_t {
        ROW_MAP     = 1 << 0,  // Engine mapping chip
        ROW_TC      = 1 << 1,  // Traction control chip
        ROW_EB      = 1 << 2,  // Engine braking chip
        ROW_AW      = 1 << 3,  // Anti-wheeling chip
        ROW_DEFAULT = ROW_MAP | ROW_TC | ROW_EB | ROW_AW
    };

    EcuWidget();
    virtual ~EcuWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Per-chip visibility (configurable via INI / settings)
    uint32_t m_enabledRows = ROW_DEFAULT;

    // Show the "TC"/"EB"/"AW" label prefix inside each assist chip (INI-only, default ON).
    // The mapping chip always shows its raw value (e.g. "1", "STD", "---").
    bool m_bShowLabels = true;

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Layout constants
    static constexpr int NUM_CHIPS = 4;            // Map (0), TC (1), EB (2), AW (3)
    static constexpr int NUM_COLS = 2;             // 2x2 grid
    static constexpr int NUM_ROWS = 2;
    static constexpr int CONTENT_CHARS = 8;        // Content width in chars (matches TyreTemp/Bars; padding added via calculateBackgroundWidth)
    static constexpr float CHIP_COL_GAP_CHARS = 0.5f;   // Horizontal gap between the two columns
    static constexpr float CONTENT_LINES = 3.0f;        // Content-area height (line-height units); matches TyreTemp/Bars
    static constexpr float CHIP_ROW_GAP_LINES = 0.3f;   // Vertical gap between the two rows

    // Color modulation factors (see plugin_utils lighten/darkenColor)
    static constexpr float ACTIVE_LIGHTEN = 0.35f; // Brighten when the aid is intervening
    static constexpr float IDLE_DARKEN = 0.55f;    // Mute the baseline chip
};

#endif // GAME_HAS_ECU
