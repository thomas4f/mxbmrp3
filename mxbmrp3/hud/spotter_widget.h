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

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // How long a cue stays on screen. Roughly spoken-length + reading time;
    // a new cue restarts the clock by replacing the text.
    static constexpr std::chrono::milliseconds kDisplayTime{5000};

    uint32_t m_lastRevision = 0;
    std::string m_text;
    std::chrono::steady_clock::time_point m_shownAt{};
};
