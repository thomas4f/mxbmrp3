// ============================================================================
// core/hud_manager_clip.cpp
// CLIPPING (core/render_clip.h): the marks a HUD recorded over its primitives
// (hud/clip_marks.h), applied by collectSurface on
// the surfaces our own renderers draw - Direct GL in game, the companion
// window. The engine's surface never comes here: its frame is the HUD's own
// arrays, byte for byte, which already hold what the engine shows.
//
// Two steps, around the copy collectSurface already makes:
//  - before it, oursSource() swaps in a copy of the HUD's arrays with each
//    OURS range back as our renderers draw it, so the drop shadows
//    collectSurface then adds follow what is actually drawn;
//  - after it (and after Motion moved it), recordClips() maps each CUT range
//    into the frame's indices, through the shadow copies inserted in front of
//    primitives, and its rect to where the HUD now stands on this surface.
//
// Pinned by tests/integration/tests/clip_marks_test.cpp.
// ============================================================================
#include "hud_manager.h"

#include <algorithm>

#include "../hud/base_hud.h"

bool HudManager::oursSource(const BaseHud& hud) {
    bool recolors = false;
    for (const ClipMarks::Mark& m : hud.m_clip.marks)
        if (m.kind != ClipMarks::Mark::CUT) { recolors = true; break; }
    if (!recolors) return false;

    const auto& quads = hud.getQuads();
    const auto& strings = hud.getStrings();
    const ClipMarks& clip = hud.m_clip;
    m_oursQuads.assign(quads.begin(), quads.end());
    m_oursStrings.assign(strings.begin(), strings.end());
    for (const ClipMarks::Mark& m : clip.marks) {
        // A mark stale against its arrays is skipped, never trusted.
        if (m.q0 < 0 || m.s0 < 0 || m.q1 < m.q0 || m.s1 < m.s0 ||
            m.q1 > static_cast<int>(quads.size()) || m.s1 > static_cast<int>(strings.size()))
            continue;
        if (m.kind == ClipMarks::Mark::OURS) {
            const size_t nq = static_cast<size_t>(m.q1 - m.q0), ns = static_cast<size_t>(m.s1 - m.s0);
            if (m.quad0 < 0 || m.color0 < 0 || static_cast<size_t>(m.quad0) + nq > clip.quads.size() ||
                static_cast<size_t>(m.color0) + ns > clip.colors.size())
                continue;
            std::copy_n(clip.quads.begin() + m.quad0, nq, m_oursQuads.begin() + m.q0);
            for (size_t i = 0; i < ns; ++i)
                m_oursStrings[static_cast<size_t>(m.s0) + i].m_ulColor = clip.colors[static_cast<size_t>(m.color0) + i];
        }
    }
    return true;
}

void HudManager::recordClips(SurfaceLayers& layers, const BaseHud& hud, size_t quadStart,
                             size_t stringStart, bool stringShadows, bool companion,
                             MotionSlot* slot) const {
    if (slot) slot->lastClips.clear();
    if (hud.m_clip.marks.empty()) return;
    const int quadCount = static_cast<int>(hud.getQuads().size());
    const int stringCount = static_cast<int>(hud.getStrings().size());
    const auto& skip = hud.getStringSkipShadow();
    // Where local primitive i begins in the frame: after the shadow copies
    // inserted in front of it and of everything before it.
    const auto outQuad = [&](int local) {
        return quadStart + static_cast<size_t>(local) + iconShiftsUpTo(&m_iconShifts, local - 1);
    };
    const auto outString = [&](int local) {
        if (!stringShadows) return stringStart + static_cast<size_t>(local);
        size_t k = stringStart;
        for (int i = 0; i < local; ++i)
            k += (static_cast<size_t>(i) < skip.size() && skip[static_cast<size_t>(i)]) ? 1 : 2;
        return k;
    };
    const float ox = companion ? hud.getCompanionOffsetX() : hud.getOffsetX();
    const float oy = companion ? hud.getCompanionOffsetY() : hud.getOffsetY();
    const Motion::Frame* f = slot && slot->drawn ? &slot->frame : nullptr;

    for (const ClipMarks::Mark& m : hud.m_clip.marks) {
        if (m.kind != ClipMarks::Mark::CUT) continue;
        if (m.q0 < 0 || m.s0 < 0 || m.q1 < m.q0 || m.s1 < m.s0 || m.q1 > quadCount || m.s1 > stringCount)
            continue;
        hudclip::Clip c;
        c.q0 = static_cast<uint32_t>(outQuad(m.q0));
        c.q1 = static_cast<uint32_t>(outQuad(m.q1));
        c.s0 = static_cast<uint32_t>(outString(m.s0));
        c.s1 = static_cast<uint32_t>(outString(m.s1));
        // Ranges keep collect order and never overlap (the cursor's contract):
        // a mark that would step back is dropped, and its range draws uncut.
        if (!layers.clips.empty() && (c.q0 < layers.clips.back().q1 || c.s0 < layers.clips.back().s1)) continue;
        float dy = 0.0f;
        if (f) {
            if (f->moving()) dy += f->dy;
            // The settings tab body's own fade moves the lists inside it.
            if (f->partMoving() && hud.m_motionPartQuadFirst >= 0 && m.q0 >= hud.m_motionPartQuadFirst &&
                m.q0 < hud.m_motionPartQuadEnd)
                dy += f->partDy;
        }
        c.l = m.l + ox; c.r = m.r + ox;
        c.t = m.t + oy + dy; c.b = m.b + oy + dy;
        layers.clips.push_back(c);
        if (slot) {
            hudclip::Clip rel = c;
            rel.q0 -= static_cast<uint32_t>(quadStart); rel.q1 -= static_cast<uint32_t>(quadStart);
            rel.s0 -= static_cast<uint32_t>(stringStart); rel.s1 -= static_cast<uint32_t>(stringStart);
            slot->lastClips.push_back(rel);
        }
    }
}

void HudManager::recordGhostClips(SurfaceLayers& layers, const MotionSlot& slot, size_t quadStart,
                                  size_t stringStart) {
    const float dy = slot.frame.dy - slot.ghostDy;
    for (hudclip::Clip c : slot.ghostClips) {
        c.q0 += static_cast<uint32_t>(quadStart); c.q1 += static_cast<uint32_t>(quadStart);
        c.s0 += static_cast<uint32_t>(stringStart); c.s1 += static_cast<uint32_t>(stringStart);
        c.t += dy; c.b += dy;
        if (!layers.clips.empty() && (c.q0 < layers.clips.back().q1 || c.s0 < layers.clips.back().s1)) continue;
        layers.clips.push_back(c);
    }
}
