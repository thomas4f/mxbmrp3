// ============================================================================
// hud/dropdown_list.cpp
// The dropdown's box and open list, drawn into any HUD (see dropdown_list.h).
// ============================================================================
#include "dropdown_list.h"
#include "base_hud.h"
#include "../core/asset_manager.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/ui_config.h"

#include <algorithm>
#include <cstring>

using namespace PluginConstants;

DropdownList::Style DropdownList::globalStyle() {
    ColorConfig& colors = ColorConfig::getInstance();
    Style style;
    style.primary = colors.getPrimary();
    style.secondary = colors.getSecondary();
    style.muted = colors.getMuted();
    style.accent = colors.getAccent();
    style.background = colors.getBackground();
    style.font = Fonts::getNormal();
    return style;
}

void DropdownList::drawBox(BaseHud& hud, float x, float rowY, float width, float rowH,
                           float fontSize, bool enabled, bool open, const Style& style) {
    const float cw = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    float boxX = x - cw * 0.3f;
    float boxY = rowY + rowH * 0.06f;
    const float boxW = width + cw * 0.3f;
    const float boxH = rowH * 0.88f;
    if (boxW > 0.0f && boxH > 0.0f) {
        SPluginQuad_t q;
        hud.applyOffset(boxX, boxY);
        BaseHud::setQuadPositions(q, boxX, boxY, boxW, boxH);
        q.m_iSprite = SpriteIndex::SOLID_COLOR;   // solid-quad-exempt: the field is a flat tint, drawn the same with or without a theme
        q.m_ulColor = PluginUtils::applyOpacity(style.primary, enabled ? 0.10f : 0.05f);
        hud.m_quads.push_back(q);
    }
    // Down while closed, up while open: the caret Rumble's split effects use,
    // in the cell after the value (unturned, the sprite points up), centred on
    // the box's height and kept off its right edge by the air the text has on
    // the left, so a value that fills the field never runs under it.
    const float caretHalf = fontSize * 0.2f;
    const float caretX = x + width - cw * 0.3f - caretHalf;
    const float caretY = rowY + rowH * 0.5f;
    const unsigned long caretColor = enabled ? style.accent : style.muted;
    // Unturned (up) while open, turned 180 (down) while closed; "^"/"v" with UI icons off.
    const bool useIcons = UiConfig::getInstance().getTitleIcons();
    const int caret = useIcons ? AssetManager::getInstance().getIconSpriteIndex("caret-up") : 0;
    if (caret > 0) hud.addRotatedSpriteQuad(caretX, caretY, caretHalf, open ? 1.0f : -1.0f, 0.0f, caret, caretColor);
    else if (!useIcons) hud.addString(open ? "^" : "v", caretX, rowY, Justify::CENTER,
                                      style.font, caretColor, fontSize);
}

// The open list, drawn LAST so its quads cover the rows beneath. The game draws
// every quad before any string, though, so a quad cannot hide text: the strings
// the list covers are blanked instead (blanked, not erased -- the title's string
// index must stay put). Where the surface's renderer stacks HUDs (Direct GL, the
// companion window) the list is the HUD's overlay instead, a layer over its
// text, and nothing is blanked (HudManager::surfaceStacks).
//
// PLACEMENT: below the box when the list fits there, else above it; a list too
// long for either side wraps into columns on the roomier side (a font or icon
// list runs to dozens), shifted left if the columns would leave the bounds.
void DropdownList::build(BaseHud& hud, const Anchor& anchor, const Bounds& bounds,
                         const Entries& entries, const Style& style, std::vector<Rect>& outEntries) {
    outEntries.clear();
    const int count = entries.count;
    if (!entries.nameOf || count <= 0) return;

    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(count));
    size_t longest = 0;
    for (int i = 0; i < count; ++i) {
        names.push_back(entries.nameOf(i));
        longest = std::max(longest, names.back().size());
    }

    const float rowH = anchor.rowH;
    const float fontSize = anchor.fontSize;
    const float pad = anchor.textX - anchor.x;
    const bool iconGrid = static_cast<bool>(entries.spriteOf);
    // A colour list leads each name with its swatch: a character and a half.
    const float swatchW = entries.swatchOf
        ? PluginUtils::calculateMonospaceTextWidth(1, fontSize) * 1.5f : 0.0f;

    // Entry geometry. A text list is one column of names, wrapped into more
    // columns on the roomier side when it fits neither below nor above the box.
    // An icon list (a few hundred icons) is a grid of square cells as wide as the
    // bounds allow, with one caption row under it naming the hovered icon.
    float cellW = 0.0f, cellH = rowH, captionH = 0.0f;
    int perCol = count;   // text: entries per column; grid: unused
    int cols = 1, rows = count;
    bool below = true;
    const float spaceBelow = bounds.bottom - (anchor.y + rowH);
    const float spaceAbove = anchor.y - bounds.top;
    if (iconGrid) {
        cellH = rowH * 1.2f;
        cellW = cellH / UI_ASPECT_RATIO;
        captionH = rowH;
        cols = std::max(1, std::min(count, static_cast<int>((bounds.right - bounds.left - pad * 2.0f) / cellW)));
        rows = (count + cols - 1) / cols;
        const float gridH = cellH * static_cast<float>(rows) + captionH;
        below = gridH <= spaceBelow || spaceBelow >= spaceAbove;
    } else {
        cellW = std::max(anchor.w,
            PluginUtils::calculateMonospaceTextWidth(static_cast<int>(longest) + 1, fontSize) + pad + swatchW);
        const int rowsBelow = static_cast<int>(spaceBelow / rowH);
        const int rowsAbove = static_cast<int>(spaceAbove / rowH);
        if (count > rowsBelow) {
            if (count <= rowsAbove) {
                below = false;
            } else {
                below = rowsBelow >= rowsAbove;
                perCol = std::max(1, below ? rowsBelow : rowsAbove);
            }
        }
        cols = (count + perCol - 1) / perCol;
        rows = std::min(count, perCol);
    }
    const float w = cellW * static_cast<float>(cols) + (iconGrid ? pad * 2.0f : 0.0f);
    const float h = cellH * static_cast<float>(rows) + captionH;
    float x = anchor.x;
    if (x + w > bounds.right) x = std::max(bounds.left, bounds.right - w);
    const float y = below ? anchor.y + rowH : anchor.y - h;

    // The menu draws only on the surface the user is on.
    const bool companion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    if (HudManager::getInstance().surfaceStacks(companion)) {
        hud.m_overlayQuadFirst = static_cast<int>(hud.m_quads.size());
        hud.m_overlayStringFirst = static_cast<int>(hud.m_strings.size());
    } else {
        // Strings are stored offset; compare against the list's offset rect.
        float ox = x, oy = y;
        hud.applyOffset(ox, oy);
        for (SPluginString_t& str : hud.m_strings) {
            const size_t len = std::strlen(str.m_szString);
            if (len == 0) continue;
            const float tw = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(len), str.m_fSize);
            float left = str.m_afPos[0];
            if (str.m_iJustify == Justify::RIGHT) left -= tw;
            else if (str.m_iJustify == Justify::CENTER) left -= tw * 0.5f;
            const float top = str.m_afPos[1];
            if (left < ox + w && left + tw > ox && top < oy + h && top + rowH * 0.8f > oy) {
                str.m_szString[0] = '\0';
            }
        }
    }

    const float edge = rowH * 0.06f;
    const int current = entries.current;
    const int hovered = entries.hovered;
    auto quad = [&hud](float qx, float qy, float qw, float qh, unsigned long color) {
        SPluginQuad_t q;
        hud.applyOffset(qx, qy);
        BaseHud::setQuadPositions(q, qx, qy, qw, qh);
        q.m_iSprite = SpriteIndex::SOLID_COLOR;   // solid-quad-exempt: the open list is a flat box over the rows, drawn the same with or without a theme
        q.m_ulColor = color;
        hud.m_quads.push_back(q);
    };
    // Outline, then an opaque body inside it.
    quad(x - edge, y - edge, w + edge * 2.0f, h + edge * 2.0f, style.accent);
    quad(x, y, w, h, PluginUtils::applyOpacity(style.background, 1.0f));

    outEntries.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float ex = iconGrid ? x + pad + cellW * static_cast<float>(i % cols)
                                  : x + cellW * static_cast<float>(i / perCol);
        const float ey = iconGrid ? y + cellH * static_cast<float>(i / cols)
                                  : y + rowH * static_cast<float>(i % perCol);
        if (i == current) {
            quad(ex, ey, cellW, cellH, PluginUtils::applyOpacity(style.accent, 0.25f));
        } else if (hovered == i) {
            quad(ex, ey, cellW, cellH, PluginUtils::applyOpacity(style.accent, 0.12f));
        }
        const unsigned long ink = i == current ? style.primary : style.secondary;
        if (iconGrid) {
            const int sprite = entries.spriteOf(i);
            if (sprite > 0) {
                hud.addIcon(ex + cellW * 0.5f, ey + cellH * 0.5f, sprite, ink, cellH * 0.7f);
            } else {
                hud.addString("-", ex + cellW * 0.5f, ey + (cellH - rowH) * 0.5f, Justify::CENTER,
                    style.font, ink, fontSize);
            }
        } else {
            if (entries.swatchOf) {
                quad(ex + pad, ey + rowH * 0.22f, swatchW * 0.66f, rowH * 0.56f, entries.swatchOf(i));
            }
            // A font list previews each face; a colour list writes each name in
            // its colour, unless that colour would vanish on the list's body.
            const int face = entries.fontOf ? entries.fontOf(i) : 0;
            unsigned long nameInk = ink;
            if (entries.swatchOf) {
                const unsigned long c = entries.swatchOf(i);
                if (!PluginUtils::isColorDark(c)) nameInk = c;
            }
            hud.addString(names[static_cast<size_t>(i)].c_str(), ex + pad + swatchW, ey, Justify::LEFT,
                face > 0 ? face : style.font, nameInk, fontSize);
        }
        outEntries.push_back({ex, ey, cellW, cellH});
    }
    if (iconGrid) {
        // The caption names what a click would pick: the hovered icon, else the current one.
        const int named = (hovered >= 0 && hovered < count) ? hovered : current;
        if (named >= 0 && named < count) {
            hud.addString(names[static_cast<size_t>(named)].c_str(), x + pad, y + h - captionH,
                Justify::LEFT, style.font, style.primary, fontSize);
        }
    }
}
