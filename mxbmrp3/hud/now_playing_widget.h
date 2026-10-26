// ============================================================================
// hud/now_playing_widget.h
// Now Playing widget - what Windows' media controls are playing (Spotify, a
// browser tab, ...): the title, the artist with the track's progress, then
// previous / play-pause / next and the player's volume, clickable while the
// pointer shows. The rows are NowPlayingCard's, which the Dash's Now Playing
// page lays out too. Needs Settings > General > Now playing (MediaManager);
// until then it says where to turn it on.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "now_playing_card.h"

class NowPlayingWidget : public BaseHud {
public:
    NowPlayingWidget();
    virtual ~NowPlayingWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // The Dash card's width, so the widget and the page cut the text alike.
    static constexpr int WIDTH_CHARS = 27;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // Once a frame: the clicks and the volume drag (NowPlayingCard::follow).
    void followInput();

    NowPlayingCard m_card;
};
