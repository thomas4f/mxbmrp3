// ============================================================================
// hud/dash_hud_ring.cpp
// Dash - see dash_hud.h. The page cycle as a ring (the pages in the cycle,
// each one's slot, the short way round between two) and the step's fade.
// ============================================================================
#include "dash_hud.h"

#include "digit_roll.h"
#include "../core/motion.h"

int DashHud::enabledCount() const {
    int n = 0;
    for (int i = 0; i < CYCLE_PAGES; ++i)
        if (pageEnabled(static_cast<Page>(i))) ++n;
    return n;
}

float DashHud::ringOffset(float d) const {
    // Two pages or fewer: a ring and a line are the same strip.
    const int n = enabledCount();
    if (n < 3) return d;
    const float count = static_cast<float>(n);
    const float half = count * 0.5f;
    while (d >= half) d -= count;
    while (d < -half) d += count;
    return d;
}

int DashHud::slotOf(Page page) const {
    int slot = 0;
    for (int i = 0; i < static_cast<int>(page); ++i)
        if (pageEnabled(static_cast<Page>(i))) ++slot;
    return slot;
}

DashHud::Page DashHud::firstEnabledFrom(Page from) const {
    const int count = CYCLE_PAGES;
    for (int i = 0; i < count; ++i) {
        const Page p = static_cast<Page>((static_cast<int>(from) + i) % count);
        if (pageEnabled(p)) return p;
    }
    return from;
}

void DashHud::stopFade() {
    m_flip.stopFade();
    m_stripFrom = 0.0f;
}
