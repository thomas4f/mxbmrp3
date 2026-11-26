// ============================================================================
// hud/page_flip.h
// One card stepping between pages: the Dash's pages, the carousel deck's
// decks. Both read the step the same way, so it lives here once.
//
// THE FADE. A step crossfades the card over Motion's fade (a cut with Motion
// off), starting from what is on screen: a step mid-fade fades out the page
// coming in from the alpha it had reached (never popping it to full), and a
// step back to the page fading out brings it in again from where it got to.
//
// THE NAME. A rider's step shows the page's name in its place for
// NAME_HOLD_MS, then the name crossfades into the content over the same fade
// (a cut with Motion off). A step while a name shows fades that name out with
// the page it belongs to (fromName), so nothing pops. Only a page coming in
// from nothing gets one: a page stepped back to mid-fade carries on bringing
// its content back from where it got to.
//
// Pages are ints (each card's own enum); the clock is Motion's
// (DigitRoll::nowUs). Pinned by dash_test ("with Page names on") and
// deck_hud_test ("the carousel steps through its decks").
// ============================================================================
#pragma once

#include "digit_roll.h"
#include "../core/motion.h"
#include "../core/ui_config.h"

struct PageFlip {
    static constexpr float NAME_HOLD_MS = 600.0f;

    int from = 0;               // the page fading out
    float fromAlpha = 0.0f;     // its alpha when the fade started
    float inFrom = 0.0f;        // the incoming page's alpha when the fade started
    float fadeMs = 0.0f;        // this fade's length; 0 = none running
    long long fadeStartUs = 0;
    bool nameUp = false;        // the page's name is showing or fading
    long long nameStartUs = 0;
    float fromName = 0.0f;      // the name's share of the page fading out

    static float motionMs() { return Motion::profileFor(UiConfig::getInstance().getMotion()).inMs; }

    // 0..1 eased progress of the fade (1 when none is running)
    float progress() const {
        if (fadeMs <= 0.0f) return 1.0f;
        const float ms = static_cast<float>(DigitRoll::nowUs() - fadeStartUs) * 0.001f;
        return ms >= fadeMs ? 1.0f : Motion::smooth(ms / fadeMs);
    }
    // The alphas of the page fading out and of the page showing
    float outAlpha() const { return fromAlpha * (1.0f - progress()); }
    float inAlpha() const { return inFrom + (1.0f - inFrom) * progress(); }
    bool fading() const { return fadeMs > 0.0f; }

    // The name's share of the page showing: 1 while held, falling to 0
    float nameShare() const {
        if (!nameUp) return 0.0f;
        const float ms = static_cast<float>(DigitRoll::nowUs() - nameStartUs) * 0.001f - NAME_HOLD_MS;
        if (ms < 0.0f) return 1.0f;
        const float fade = motionMs();
        if (fade <= 0.0f || ms >= fade) return 0.0f;
        return 1.0f - Motion::smooth(ms / fade);
    }

    // A step from page `cur` to `next`. Returns the progress the fade had
    // reached, for what else moves with it (the Dash's strip).
    float go(int cur, int next) {
        const float e = progress();
        const float inNow = inFrom + (1.0f - inFrom) * e;
        const float outNow = fromAlpha * (1.0f - e);
        inFrom = next == from ? outNow : 0.0f;
        from = cur;
        fromAlpha = inNow;
        // The name showing leaves with its page; the new page opens on its
        // content unless the step that brought it calls showName()
        fromName = nameUp ? nameShare() : 0.0f;
        nameUp = false;
        fadeMs = motionMs();
        fadeStartUs = DigitRoll::nowUs();
        if (fadeMs <= 0.0f) stopFade();
        return e;
    }
    // After go(): the page's name, for a page coming in from nothing
    void showName() {
        if (inFrom > 0.0f) return;
        nameUp = true;
        nameStartUs = DigitRoll::nowUs();
    }
    void stopFade() {
        fadeMs = 0.0f;
        fromAlpha = 0.0f;
        inFrom = 0.0f;
    }
    void clearName() {
        nameUp = false;
        fromName = 0.0f;
    }
    // Once a frame: whether the card redraws. Every frame while the fade or
    // a name fading runs; held, or with Motion off, once more when it ends.
    bool tick() {
        bool redraw = false;
        if (fadeMs > 0.0f) {
            redraw = true;
            if (progress() >= 1.0f) stopFade();
        }
        if (nameUp) {
            const float share = nameShare();
            if (share <= 0.0f) {
                nameUp = false;
                redraw = true;
            } else if (share < 1.0f) {
                redraw = true;
            }
        }
        return redraw;
    }
};
