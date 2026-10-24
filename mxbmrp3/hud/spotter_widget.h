// ============================================================================
// hud/spotter_widget.h
// Spotter subtitle widget — shows the most recent spotter cue as on-screen
// text for a few seconds. Exists for two audiences: players who want the
// callouts without (or in addition to) audio, and development/testing, where
// reading what the spotter decided beats listening for it. Content comes from
// SpotterManager's cue log; a revision counter makes the per-frame poll one
// atomic load, so silence costs nothing.
// ============================================================================
#pragma once

#include <chrono>
#include <string>

#include "base_hud.h"

class SpotterWidget : public BaseHud {
public:
    SpotterWidget();
    virtual ~SpotterWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // THE PREVIEW CARD'S WIDTH at a given scale, unthemed: the sample cue's
    // monospace estimate plus the unthemed box's horizontal air, before
    // fitPanelToGrid rounds the drawn box up to whole cells. Public and static
    // because the settings v12 migration needs it without a widget to ask: it
    // converts a stored LEFT edge into a stored CENTRE, and this is the width
    // that conversion is half of -- so a card the player dragged keeps its
    // preview where they put it. spotter_position_test pins the agreement.
    static float previewCardWidth(float scale);

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // A real cue's SHAPE, so the preview card is the width one will actually
    // be, with the word that says why it is on screen at all. Never routed
    // through SpotterManager -- nothing is spoken, logged or sent to the overlay.
    static constexpr char kSampleCue[] = "Preview - rider on your inside";

    // How long a cue stays on screen. Roughly spoken-length + reading time;
    // a new cue restarts the clock by replacing the text.
    static constexpr std::chrono::milliseconds kDisplayTime{5000};

    uint32_t m_lastRevision = 0;
    std::string m_text;
    std::chrono::steady_clock::time_point m_shownAt{};
};
