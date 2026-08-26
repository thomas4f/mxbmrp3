// ============================================================================
// hud/gear_geometry.h
// How big the gear DIGIT is, and where in its row it sits. Pulled out of
// gear_widget.cpp so the sizing rule can be exercised without a game: it is a
// pure function of the base font size and the box the widget laid out.
// ============================================================================
#pragma once

#include <algorithm>

#include "../core/layout_metrics.h"

namespace GearGeometry {

// The gear number is drawn in the TITLE font, filling the widget's two-line row.
// GEAR_FONT_SIZE_EXTRA + GEAR_TEXT_OFFSET_Y were tuned (057f491) for the old bitmap
// fonts, whose digits inked ~60% of the cell. The later font normalization (074e22a)
// rebuilt every .fnt to a consistent, smaller digit -- measured ~45.9% of the cell for
// EnterSansman, and nudged lower in the cell -- so at the same requested size the gear
// digit renders 0.459/0.600 = 0.77x as tall (and sits a little low). NUMBER_FONT_SCALE
// restores the digit HEIGHT (0.600/0.459 = 1.306) and the deeper text offset
// re-centers it (the scale pushes the ink centre down ~0.175*fontSize). This scales only
// the number + limiter circle; the widget background is sized from its row height, so it
// is unchanged. Tune NUMBER_FONT_SCALE to taste (1.35 also reads well).
//
// RATIOS, not absolute normalized lengths, and that is the fix as much as the
// numbers are: both were absolute, so they held at the default text size and
// drifted at every other one -- the widget's box is built from line heights and
// scales, while the nudge that centres the digit inside it did not. Same class of
// bug as the standings row icons.
constexpr float NUMBER_FONT_SCALE = 1.3f;      // Restore pre-normalization digit height
// Extra size beyond the row height, as a fraction OF that row (was 0.01944 flat,
// which is this ratio at the shipped default -- so the digit is unchanged there).
constexpr float FONT_SIZE_EXTRA_RATIO = 0.2761f;
// (A TEXT_OFFSET_RATIO lived here: a hand-tuned vertical nudge, a fraction of the
// gear font, that put the digit's ink mid-row. It only worked because the font was
// itself a multiple of the row, so the two scaled with uiLineHeight together and the
// ratio held by coincidence -- and the moment the font stopped tracking the row, the
// digit sat high. BaseHud::inkCenteredY solves the same placement from the font's own
// measured ink centre, for an arbitrary box at an arbitrary size, so the widget uses
// that and there is nothing to tune.)

// The size that fills a row `rowHeight` tall.
inline float boxDrivenFontSize(float rowHeight) {
    return rowHeight * (1.0f + FONT_SIZE_EXTRA_RATIO) * NUMBER_FONT_SCALE;
}

// The same expression at the SHIPPED row metrics, per base font size. Derived from
// LayoutMetrics rather than written as a literal so a change to the default
// lineHeightRatio (or rowL) moves the digit and its box together.
inline float perFontSize() {
    static const float ratio = [] {
        LayoutMetrics m{};
        m.derive();
        return boxDrivenFontSize(m.lineHeightLarge + m.lineHeightNormal) / m.fontSizeNormal;
    }();
    return ratio;
}

// THE DIGIT'S SIZE: from the FONT, capped by the BOX.
//
// It used to be boxDrivenFontSize(rowHeight) alone, and that row is three
// lineHeightNormal -- so [Advanced] uiLineHeight, which is ROW PITCH, scaled the
// digit. The box's WIDTH is a character count at the base font and uiLineHeight
// does not move it, so the two ran apart: at 1.8 the numeral was half again as
// wide as the panel it is drawn in and hung off both sides. Every other element
// answers a raised uiLineHeight with more air and the same glyphs -- SpeedWidget,
// whose content height this widget matches, keeps its value at the base font --
// and this one answered with a bigger glyph.
//
// The cap keeps the other direction honest: a LOWERED uiLineHeight shortens the
// box, and the old expression is exactly the size that fills it. The two are equal
// at the shipped defaults, which is why the widget is pixel-identical there.
inline float fontSize(float baseFontSize, float rowHeight) {
    return std::min(baseFontSize * perFontSize(), boxDrivenFontSize(rowHeight));
}

}  // namespace GearGeometry
