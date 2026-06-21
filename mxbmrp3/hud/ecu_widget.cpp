// ============================================================================
// hud/ecu_widget.cpp
// ECU Widget - GP Bikes electronic rider aids as a row of chips
// Chips: Map | TC | EB | AW. Brightness tracks live intervention (ecuState);
// an underline marks the page the rider is currently adjusting (ecuMode).
// ============================================================================
#include "ecu_widget.h"

#if GAME_HAS_ECU

#include <cstdio>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

namespace {
    // ecuState bitfield masks (gpb_api.h: bit1=TC, bit2=EB, bit3=AW active)
    constexpr int MASK_TC = 0x1;
    constexpr int MASK_EB = 0x2;
    constexpr int MASK_AW = 0x4;
}

EcuWidget::EcuWidget() {
    DEBUG_INFO("EcuWidget created");
    setDraggable(true);

    // Reserve render data:
    // - 1 background quad + up to 4 chip quads + 1 active-page underline
    m_quads.reserve(6);
    // - up to 4 chip strings
    m_strings.reserve(4);

    setTextureBaseName("ecu_widget");

    resetToDefaults();

    rebuildRenderData();
}

bool EcuWidget::handlesDataType(DataChangeType dataType) const {
    // ECU values ride along with the telemetry stream; rebuild on spectate change too
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void EcuWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - ECU values update at telemetry rate
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void EcuWidget::rebuildLayout() {
    rebuildRenderData();
}

void EcuWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // ECU data is only meaningful while the player is on track
    bool onTrack = (pluginData.getDrawState() == ViewState::ON_TRACK);
    bool hasData = onTrack && bikeData.isValid;

    // Per-chip static descriptors (fixed Map/TC/EB/AW order)
    struct ChipDef {
        uint32_t rowFlag;
        ColorSlot baseSlot;
        int activeMask;     // 0 = no live-intervention state (mapping)
        const char* label;  // prefix shown when m_bShowLabels (mapping uses none)
    };
    static const ChipDef CHIPS[NUM_CHIPS] = {
        { ROW_MAP, ColorSlot::NEUTRAL,  0,       ""   },
        { ROW_TC,  ColorSlot::NEGATIVE, MASK_TC, "TC" },
        { ROW_EB,  ColorSlot::WARNING,  MASK_EB, "EB" },
        { ROW_AW,  ColorSlot::ACCENT,   MASK_AW, "AW" },
    };

    // Standard widget sizing: CONTENT_CHARS of content inset by paddingH (~1 char)
    // on the left/right and paddingV on the top/bottom, matching the other widgets.
    float startX = 0.0f;
    float startY = 0.0f;

    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    float contentWidth = CONTENT_CHARS * charWidth;
    float backgroundWidth = calculateBackgroundWidth(CONTENT_CHARS);

    // Content area is CONTENT_LINES tall (paddingV top+bottom) so this widget sits at
    // the same height as TyreTemp/Bars. The two chip rows split the content area evenly
    // with a gap between them.
    float rowGap = CHIP_ROW_GAP_LINES * dim.lineHeightNormal;
    float contentHeight = CONTENT_LINES * dim.lineHeightNormal;
    float chipRowHeight = (contentHeight - (NUM_ROWS - 1) * rowGap) / NUM_ROWS;
    float backgroundHeight = contentHeight + 2.0f * dim.paddingV;

    // Background + drag bounds
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // 2x2 chip grid, inset by the standard padding so quads stay within bounds
    float colGap = CHIP_COL_GAP_CHARS * charWidth;
    float chipWidth = (contentWidth - (NUM_COLS - 1) * colGap) / NUM_COLS;
    float gridLeft = startX + dim.paddingH;
    float gridTop = startY + dim.paddingV;

    // Small font (like TyreTemp): at 8-char content each chip column is only
    // ~3.75 chars wide, so a labelled value ("TC100") would overrun at the
    // normal font. The small font keeps the chip text inside its column.
    float valueFontSize = dim.fontSizeSmall;

    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);

    int ecuMode = bikeData.ecuMode;  // 0=map, 1=TC, 2=EB (never points at AW)

    for (int chip = 0; chip < NUM_CHIPS; ++chip) {
        if (!(m_enabledRows & CHIPS[chip].rowFlag)) continue;  // hidden chip leaves its grid cell empty
        const ChipDef& def = CHIPS[chip];

        int col = chip % NUM_COLS;
        int row = chip / NUM_COLS;
        float chipX = gridLeft + col * (chipWidth + colGap);
        float chipY = gridTop + row * (chipRowHeight + rowGap);
        float chipCenterX = chipX + chipWidth * 0.5f;
        float textY = chipY + (chipRowHeight - valueFontSize) * 0.5f;

        // Chip background color: muted baseline, brightened when the aid intervenes
        unsigned long chipColor;
        if (!hasData) {
            chipColor = PluginUtils::darkenColor(mutedColor, IDLE_DARKEN);
        } else {
            unsigned long base = this->getColor(def.baseSlot);
            bool active = (def.activeMask != 0) && ((bikeData.ecuState & def.activeMask) != 0);
            chipColor = active ? PluginUtils::lightenColor(base, ACTIVE_LIGHTEN)
                               : PluginUtils::darkenColor(base, IDLE_DARKEN);
        }
        // Chips are the parameter readout, not the panel backdrop — they keep their own
        // alpha so they stay visible regardless of the background-opacity slider (only
        // addBackgroundQuad follows that).

        SPluginQuad_t chipQuad;
        float cx = chipX, cy = chipY;
        applyOffset(cx, cy);
        setQuadPositions(chipQuad, cx, cy, chipWidth, chipRowHeight);
        chipQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        chipQuad.m_ulColor = chipColor;
        m_quads.push_back(chipQuad);

        // Active-page underline: marks the chip the rider's ECU buttons will adjust.
        // Kept visually separate from the brightness signal above.
        if (hasData && chip == ecuMode) {
            float underlineThickness = rowGap * 0.5f;
            float underlineY = chipY + chipRowHeight - underlineThickness;
            SPluginQuad_t underlineQuad;
            float ux = chipX, uy = underlineY;
            applyOffset(ux, uy);
            setQuadPositions(underlineQuad, ux, uy, chipWidth, underlineThickness);
            underlineQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            underlineQuad.m_ulColor = textColor;  // part of the readout, not the backdrop — see chip quad above
            m_quads.push_back(underlineQuad);
        }

        // Chip text
        char buffer[16];
        unsigned long chipTextColor = textColor;
        if (!hasData) {
            const char* placeholder = onTrack ? Placeholders::GENERIC : Placeholders::NOT_AVAILABLE;
            snprintf(buffer, sizeof(buffer), "%s", placeholder);
            chipTextColor = mutedColor;
        } else if (chip == 0) {
            // Mapping chip: raw value (e.g. "1", "STD", "---"); never labelled, matching the reference
            const char* mapping = bikeData.engineMapping;
            snprintf(buffer, sizeof(buffer), "%s", (mapping && mapping[0]) ? mapping : Placeholders::GENERIC);
        } else {
            int value = (chip == 1) ? bikeData.tractionControl
                      : (chip == 2) ? bikeData.engineBraking
                                    : bikeData.antiWheeling;
            if (m_bShowLabels) {
                snprintf(buffer, sizeof(buffer), "%s%d", def.label, value);
            } else {
                snprintf(buffer, sizeof(buffer), "%d", value);
            }
        }

        addString(buffer, chipCenterX, textY, Justify::CENTER,
            this->getFont(FontCategory::STRONG), chipTextColor, valueFontSize);
    }
}

void EcuWidget::resetToDefaults() {
    m_bVisible = false;      // Hidden by default; opt-in via the Widgets tab
    m_bShowTitle = false;    // No title for gauge-style widgets
    setTextureVariant(0);    // No texture by default
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;
    // GP-only widget: sits left of the Tyre Temp widget in the bottom gauge row (pitch 0.0715).
    setPosition(0.385f, 0.8769f);
    m_enabledRows = ROW_DEFAULT;
    m_bShowLabels = true;    // Labels ON by default (INI/settings toggle)
    setDataDirty();
}

#endif // GAME_HAS_ECU
