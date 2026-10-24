// ============================================================================
// core/hud_manager_sample.cpp
// The HOVER PREVIEW's mark: a switched-off HUD drawn while its Toggles strip
// icon is hovered (HudManager::setPreviewHud) must not read as switched on, so
// its primitives are dimmed on their way into the frame, after the HUD built
// them. Nothing in the HUD changes, so the mark costs nothing until a sample is
// up, and then one pass over that one HUD's primitives.
// ============================================================================
#include "hud_manager.h"

namespace {
// What the sample keeps of its own alpha.
constexpr unsigned long kSampleAlpha255 = 128;

unsigned long dimmed(unsigned long color) {
    const unsigned long a = ((color >> 24) & 0xFF) * kSampleAlpha255 / 255;
    return (color & 0x00FFFFFF) | (a << 24);
}
}  // namespace

void HudManager::markSample(std::vector<SPluginQuad_t>& outQuads, size_t quadStart,
                            std::vector<SPluginString_t>& outStrings, size_t stringStart) {
    for (size_t k = quadStart; k < outQuads.size(); ++k) outQuads[k].m_ulColor = dimmed(outQuads[k].m_ulColor);
    for (size_t k = stringStart; k < outStrings.size(); ++k) outStrings[k].m_ulColor = dimmed(outStrings[k].m_ulColor);
}
