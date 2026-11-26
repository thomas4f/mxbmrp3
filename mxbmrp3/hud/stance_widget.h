// ============================================================================
// hud/stance_widget.h
// Stance widget - whether the rider is standing on the pegs or seated
//
// ONE ICON, THREE WAYS (INI-only `mode`, no room for options on a widget):
//   pair     -- two icons, one pointing up and one turned over, the one for the
//               current stance in the theme's ACCENT and the other MUTED (default);
//   flip     -- one icon, pointing up while standing and turned over while seated;
//   standing -- one icon, drawn only while standing.
// With nothing to read (spectating, or the bike not yet on track) the pair and
// the flip icon are muted, like the Speed and Gear placeholders, and `standing`
// draws nothing. The icon is any icon by NAME (INI-only `icon`, default play), the
// way Map and Gap Bar store their marker, drawn pointing up and turned 180 degrees
// for seated.
//
// THE GEAR WIDGET'S BOX AND INK: the same content width and row, and the icons'
// squares span the gear digit's ink height (GearGeometry::DIGIT_INK_SHARE),
// centred in the row the way the digit is -- sized by the square, as every icon
// is drawn, so a glyph that does not fill its square comes out smaller.
//
// ONLY WHERE THE GAME SENDS IT: the stance is the int newer MX Bikes builds append
// to the bike telemetry (Adapter::readRiderStance). Until the game has sent one
// (core/rider_stance.h) the widget draws nothing and clears its bounds, and the
// Widgets tab leaves its row out -- the Prestige rule (prestige_widget.h), one
// question asked in both places. Developer mode answers it too, so the widget can
// be laid out on a build without it. MX Bikes only (GAME_HAS_RIDER_STANCE).
// ============================================================================
#pragma once

#include "base_hud.h"

class StanceWidget : public BaseHud {
    friend class SettingsHud;
    friend class SettingsManager;

public:
    StanceWidget();
    virtual ~StanceWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // The game sends a stance, or developer mode is on.
    static bool isAvailable();

    enum class Mode { Pair = 0, Flip, StandingOnly };
    static constexpr const char* DEFAULT_ICON = "play";

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // What the last rebuild drew: the stance moves a few times a lap, so the
    // widget rebuilds when it changes rather than every frame.
    bool m_builtAvailable = false;
    int m_builtStance = -1;

    Mode m_mode = Mode::Pair;
    int m_iconShape = 0;   // 1-based icon shape (settings_serde.h); 0 = DEFAULT_ICON
};
