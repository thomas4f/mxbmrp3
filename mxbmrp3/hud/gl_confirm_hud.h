// ============================================================================
// hud/gl_confirm_hud.h
// "Can you still read this?" - the dead-man's switch on Direct GL Rendering.
//
// WHY IT EXISTS. Direct GL draws the whole HUD itself, and the way it fails on
// an unfamiliar driver is not a blank screen but a WRONG one: a tester's build
// rendered panels correctly while every glyph and icon came out a solid block.
// The HUD was still there, still laid out, still clickable - and completely
// unreadable. So the user could not find the setting to turn it back off, and
// recovery meant hand-editing the INI or wiping the install.
//
// IT IS DRAWN BY DIRECT GL, like everything else. That is the point, and it was
// the second design: the first routed these primitives to the ENGINE so they
// would stay readable whatever GL did. That version is worse, for two reasons
// that only became obvious once it existed.
//
// It would render perfectly no matter how badly GL was misdrawing, so clicking
// "Keep" proved nothing at all - and a player whose HUD is all gauges and no text
// would have had nothing to judge by even if they wanted to. A prompt that asks
// "can you read this?" has to be drawn by the thing being asked about, or it is
// not a question, it is decoration.
//
// The countdown is what makes that safe, and why it is a BAR rather than a number
// of seconds. The observed failure took glyphs and icons while quads kept
// drawing correctly, so a user who cannot read a word of this panel still sees a
// block shrinking and a coloured button, and can either wait it out or press the
// red one. Nothing here needs to be legible for the escape to work.
//
// TIME ADVANCES ONLY WHILE DIRECT GL IS ACTUALLY DRAWING. The game issues no
// Draw callbacks in menus, so a plain wall-clock countdown would expire while
// the player sat in the pause menu and revert a setting they were never shown
// the question about. What is being measured is "how long has this been in front
// of them".
//
// It is ticked from update(), which is only ever reached from produceFrame,
// which is only ever reached from the game's Draw callback - so no Draw means no
// time, and the menu case needs no code at all. Two earlier versions each got
// this wrong in an instructive way: the first had renderInContextGl push the
// tick in from the GAME thread while update() ran on the worker, buying a
// cross-thread hazard no other HUD has; the second gated on glDrewLastFrame(),
// which is cleared on every entry to renderInContextGl and set only when it
// draws - so on the pluginThread path, where the game thread calls it every
// frame including with empty frames, most samples read false and each one threw
// its interval away. Ten seconds became minutes.
//
// Silence reverts. Confirming is a deliberate act; doing nothing is not, and the
// user who cannot read the prompt is precisely the user who must end up back on
// engine rendering.
// ============================================================================
#pragma once

#include "base_hud.h"

#include <chrono>
#include "../core/plugin_constants.h"

class GlConfirmHud : public BaseHud {
public:
    GlConfirmHud();
    virtual ~GlConfirmHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    // A question mark, not the Performance tab's gauge this borrowed at first.
    // The panel asks "can you still read this?"; a speedometer answers nothing,
    // and the icon is one of the few things a player whose glyphs are drawing as
    // solid blocks might still recognise by shape.
    const char* getIconName() const override { return "hud-confirm"; }

    // Start the countdown. Called when the user turns Direct GL ON - not at
    // startup with it already on, because that user answered this question in the
    // session where they enabled it.
    void arm();
    // Stop it without changing the setting (the backend latched off by itself, so
    // the engine is already drawing and there is nothing left to confirm).
    void cancel();

    bool isActive() const { return m_active; }
    // 0..1, for the countdown bar and for tests.
    float remainingFraction() const;

    // Advance the countdown by `dtSeconds` of on-screen time. update() calls this
    // itself; it is public only so a test can drive twenty seconds without
    // rendering for twenty seconds.
    void tickDrawn(float dtSeconds);

    // How long the user gets, in seconds of DRAWN time - it does not run down in
    // menus, so this is ten seconds of actually looking at the thing, which is
    // ample for two lines and a button. INI-tunable would be a setting nobody
    // wants; a test hook drives it instead (MXBMRP3_Test_GlConfirm*).
    static constexpr float TIMEOUT_SECONDS = 10.0f;

    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void handleClickDetection();
    void keep();
    void revertNow();

    // PLAIN, like every other HUD's state: only the thread running produceFrame
    // ever touches these. See the header note on where the tick comes from.
    bool m_active = false;
    float m_remaining = 0.0f;
    // Wall-clock of the last update() that counted, so the countdown measures
    // real seconds rather than frames (it must mean the same thing at 60 and 480
    // fps). Zero = no previous counted frame, so the first one costs nothing.
    std::chrono::steady_clock::time_point m_lastTick{};

    // Rebuild only when the countdown crosses a whole percent: this is drawn every
    // frame while it is up, and the bar cannot show more than that anyway.
    int m_shownPercent = -1;

    // Button bounds, stored at rebuild and hit-tested in update() - the shape
    // CrashWidget and VersionWidget both use.
    float m_keepLeft = 0.0f, m_keepTop = 0.0f, m_keepW = 0.0f, m_keepH = 0.0f;
    float m_revertLeft = 0.0f, m_revertTop = 0.0f, m_revertW = 0.0f, m_revertH = 0.0f;
    bool m_keepHovered = false;
    bool m_revertHovered = false;
    bool m_wasLeftPressed = false;
};
