// ============================================================================
// core/hud_manager_motion.cpp
// Motion's collect side: HUDs fade and slide in and out on the copy of their
// primitives collectSurface hands each surface. The timing and easing are
// core/motion.h; this file decides who is drawing, keeps the ghosts of HUDs
// that just stopped, and applies the step to the output ranges. See the
// MotionSlot comment in hud_manager.h.
//
// MOTION OFF TOUCHES NOTHING: motionBeginFrame returns null and collectSurface
// runs exactly the copy it always did (motion_test pins the frame bytes). With
// Motion on, a HUD that is settled is not touched either - the step says so and
// its range is only recorded, four integers.
//
// The digit roll (hud/digit_roll.h) is Motion too, and runs on this clock.
// ============================================================================
#include "hud_manager.h"
#include "ui_config.h"
#include "../hud/digit_roll.h"
#include "../hud/pointer_widget.h"
#include "../hud/settings_hud.h"

#include <algorithm>
#include <chrono>

namespace {

#if defined(MXBMRP3_TEST_BUILD)
long long s_testMotionNowUs = -1;
#endif

long long motionNowUs() {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testMotionNowUs >= 0) return s_testMotionNowUs;
#endif
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void moveRange(std::vector<SPluginQuad_t>& quads, size_t q0, size_t q1,
               std::vector<SPluginString_t>& strings, size_t s0, size_t s1,
               float alpha, float dy) {
    for (size_t k = q0; k < q1; ++k) {
        SPluginQuad_t& q = quads[k];
        q.m_ulColor = Motion::scaleAlpha(q.m_ulColor, alpha);
        for (int c = 0; c < 4; ++c) q.m_aafPos[c][1] += dy;
    }
    for (size_t k = s0; k < s1; ++k) {
        SPluginString_t& s = strings[k];
        s.m_ulColor = Motion::scaleAlpha(s.m_ulColor, alpha);
        s.m_afPos[1] += dy;
    }
}

}  // namespace

bool DigitRoll::enabled() { return UiConfig::getInstance().getMotion() != Motion::Level::OFF; }
long long DigitRoll::nowUs() { return motionNowUs(); }

#if defined(MXBMRP3_TEST_BUILD)
void HudManager::testSetMotionNowUs(long long us) { s_testMotionNowUs = us; }

bool HudManager::testMotionUnderAlpha(const BaseHud* hud, int& under, int& over) const {
    for (size_t i = 0; i < m_huds.size() && i < m_motionGame.slots.size(); ++i) {
        if (m_huds[i].get() != hud) continue;
        const MotionSlot& slot = m_motionGame.slots[i];
        if (!slot.lastValid || slot.lastUnderCount == 0) return false;
        const size_t u0 = slot.lastQuad + slot.lastUnderFirst;
        const size_t u1 = u0 + slot.lastUnderCount;
        if (u1 >= m_quads.size()) return false;
        under = 0;
        for (size_t k = u0; k < u1; ++k)
            under = std::max(under, static_cast<int>((m_quads[k].m_ulColor >> 24) & 0xFF));
        over = static_cast<int>((m_quads[u1].m_ulColor >> 24) & 0xFF);
        return true;
    }
    return false;
}
#endif

bool HudManager::drawsOnSurface(const BaseHud* hud, bool companion, bool surfaceIsActive) const {
    if (!hud) return false;
    // rendersOnCompanion() gates the companion pass BEFORE its on/off: a HUD that
    // is an in-game effect rather than a panel never belongs on the second
    // surface, whatever its companion visibility says. See BaseHud.
    const bool visible = companion ? (hud->rendersOnCompanion() && hud->getCompanionVisible())
                                   : hud->isVisible();
    // Not drawn for a reason other than its own flag (the Direct GL prompt
    // holding the menu back, the hide-all hotkey, the widgets toggle): the
    // same answer the input pass reads, so what is off screen takes nothing.
    if (!visible || isHeldBack(hud)) return false;
    // The pointer and the open settings MENU render only on the active surface
    // (the settings BUTTON stays on both - it's how you open settings there).
    if ((hud == m_pPointer || hud == m_pSettingsHud) && !surfaceIsActive) return false;
    return true;
}

HudManager::MotionSurface* HudManager::motionBeginFrame(
        bool companion, bool surfaceIsActive,
        const std::vector<SPluginQuad_t>& outQuads,
        const std::vector<SPluginString_t>& outStrings) {
    MotionSurface& ms = companion ? m_motionCompanion : m_motionGame;
    const Motion::Level level = UiConfig::getInstance().getMotion();
    if (level == Motion::Level::OFF) {
        // Forget everything, so switching Motion on later starts every HUD settled
        // rather than fading in whatever changed in between.
        if (!ms.slots.empty()) ms.slots.clear();
        ms.lastUs = -1;
        return nullptr;
    }
    if (ms.slots.size() != m_huds.size()) ms.slots.assign(m_huds.size(), MotionSlot{});

    const long long now = motionNowUs();
    const float dtMs = ms.lastUs < 0 ? 0.0f : static_cast<float>(now - ms.lastUs) / 1000.0f;
    ms.lastUs = now;
    const Motion::Profile profile = Motion::profileFor(level);
    Motion::Profile fadeOnly = profile;
    fadeOnly.risePx = 0.0f;

    for (size_t i = 0; i < m_huds.size(); ++i) {
        MotionSlot& slot = ms.slots[i];
        const BaseHud* hud = m_huds[i].get();
        // A HUD that does not take Motion (BaseHud::motionStyle) pops as before.
        const BaseHud::MotionStyle style = hud ? hud->motionStyle() : BaseHud::MotionStyle::NONE;
        if (style == BaseHud::MotionStyle::NONE) {
            slot.frame = Motion::Frame{};
            slot.drawn = false;
            slot.lastValid = false;
            slot.ghostLive = false;
            continue;
        }
        // Content counts too: a HUD that draws nothing until it has something to
        // say (a notice, a toast, the pit board between splits) appears when it does.
        const bool drawnNow = drawsOnSurface(hud, companion, surfaceIsActive) &&
                              !(hud->getQuads().empty() && hud->getStrings().empty());
        slot.frame = Motion::step(slot.track, drawnNow, hud->m_motionPartEpoch, dtMs,
                                  style == BaseHud::MotionStyle::FADE ? fadeOnly : profile);

        if (drawnNow || !slot.frame.emit) {
            slot.ghostLive = false;
        } else if (slot.lastValid) {
            // It drew last frame and does not now: keep what it handed over then.
            // The range is checked against the buffer, which something other than
            // collectSurface may have cleared since (HudManager::clear()).
            const bool fits = slot.lastQuad + slot.lastQuadCount <= outQuads.size() &&
                              slot.lastString + slot.lastStringCount <= outStrings.size();
            const auto q0 = outQuads.begin() + static_cast<std::ptrdiff_t>(slot.lastQuad);
            const auto s0 = outStrings.begin() + static_cast<std::ptrdiff_t>(slot.lastString);
            slot.ghostLive = fits && slot.lastAlpha > 0.001f;
            if (slot.ghostLive) {
                slot.ghostQuads.assign(q0, q0 + static_cast<std::ptrdiff_t>(slot.lastQuadCount));
                slot.ghostStrings.assign(s0, s0 + static_cast<std::ptrdiff_t>(slot.lastStringCount));
                slot.ghostAlpha = slot.lastAlpha;
                slot.ghostDy = slot.lastDy;
                slot.ghostUnderFirst = slot.lastUnderFirst;
                slot.ghostUnderCount = slot.lastUnderCount;
            }
        }
        slot.drawn = drawnNow;
        slot.lastValid = false;   // set again by motionFinishHud if it draws
    }
    return &ms;
}

void HudManager::motionEmitGhost(const MotionSlot& slot, std::vector<SPluginQuad_t>& outQuads,
                                 std::vector<SPluginString_t>& outStrings) {
    const size_t q0 = outQuads.size();
    const size_t s0 = outStrings.size();
    outQuads.insert(outQuads.end(), slot.ghostQuads.begin(), slot.ghostQuads.end());
    outStrings.insert(outStrings.end(), slot.ghostStrings.begin(), slot.ghostStrings.end());
    // The ghost already carries the step it was captured with; apply the change since.
    const float alpha = std::clamp(slot.frame.alpha / slot.ghostAlpha, 0.0f, 1.0f);
    moveRange(outQuads, q0, outQuads.size(), outStrings, s0, outStrings.size(),
              alpha, slot.frame.dy - slot.ghostDy);
    // The under layer was captured at ghostAlpha cubed and goes on cubed.
    if (slot.ghostUnderCount > 0)
        moveRange(outQuads, q0 + slot.ghostUnderFirst, q0 + slot.ghostUnderFirst + slot.ghostUnderCount,
                  outStrings, 0, 0, alpha * alpha, 0.0f);
}

void HudManager::motionFinishHud(MotionSlot& slot, const BaseHud& hud,
                                 std::vector<SPluginQuad_t>& outQuads, size_t quadStart,
                                 std::vector<SPluginString_t>& outStrings, size_t stringStart,
                                 int titleIconShiftAt, bool stringShadows) {
    if (!slot.drawn) return;
    const Motion::Frame& f = slot.frame;
    if (f.moving())
        moveRange(outQuads, quadStart, outQuads.size(), outStrings, stringStart, outStrings.size(),
                  f.alpha, f.dy);

    // Local quad indices map through the title icon's shadow copy, which
    // collectSurface inserts before titleIconShiftAt.
    const auto outQuad = [&](int local) {
        return quadStart + static_cast<size_t>(local) +
               ((titleIconShiftAt >= 0 && local >= titleIconShiftAt) ? 1u : 0u);
    };

    // The under layer (the map's outline) on alpha cubed: two more factors of it.
    slot.lastUnderFirst = slot.lastUnderCount = 0;
    const int uEnd = std::min(hud.m_motionUnderQuadEnd, static_cast<int>(hud.getQuads().size()));
    if (hud.m_motionUnderQuadFirst >= 0 && uEnd > hud.m_motionUnderQuadFirst) {
        const size_t u0 = outQuad(hud.m_motionUnderQuadFirst);
        const size_t u1 = outQuad(uEnd - 1) + 1;
        slot.lastUnderFirst = u0 - quadStart;
        slot.lastUnderCount = u1 - u0;
        if (f.alpha < 1.0f)
            moveRange(outQuads, u0, u1, outStrings, 0, 0, f.alpha * f.alpha, 0.0f);
    }

    // The HUD's marked sub-range (the settings tab body) on its own fade. Strings
    // also map through the shadow string before each shadowed string.
    if (f.partMoving() && hud.m_motionPartQuadFirst >= 0) {
        const int qEnd = std::min(hud.m_motionPartQuadEnd, static_cast<int>(hud.getQuads().size()));
        if (qEnd > hud.m_motionPartQuadFirst)
            moveRange(outQuads, outQuad(hud.m_motionPartQuadFirst), outQuad(qEnd - 1) + 1,
                      outStrings, 0, 0, f.partAlpha, f.partDy);
        const int sFirst = hud.m_motionPartStringFirst;
        const int sEnd = std::min(hud.m_motionPartStringEnd, static_cast<int>(hud.getStrings().size()));
        if (sFirst >= 0 && sEnd > sFirst) {
            if (!stringShadows) {
                moveRange(outQuads, 0, 0, outStrings, stringStart + static_cast<size_t>(sFirst),
                          stringStart + static_cast<size_t>(sEnd), f.partAlpha, f.partDy);
            } else {
                const auto& skip = hud.getStringSkipShadow();
                size_t k = stringStart;
                for (int i = 0; i < sEnd; ++i) {
                    const bool shadowed = !(static_cast<size_t>(i) < skip.size() && skip[static_cast<size_t>(i)]);
                    const size_t first = k;
                    k += shadowed ? 2 : 1;
                    if (i >= sFirst)
                        moveRange(outQuads, 0, 0, outStrings, first, k, f.partAlpha, f.partDy);
                }
            }
        }
    }

    slot.lastValid = true;
    slot.lastQuad = quadStart;
    slot.lastQuadCount = outQuads.size() - quadStart;
    slot.lastString = stringStart;
    slot.lastStringCount = outStrings.size() - stringStart;
    slot.lastAlpha = f.alpha;
    slot.lastDy = f.dy;
}
