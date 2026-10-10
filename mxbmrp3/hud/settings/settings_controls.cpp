// ============================================================================
// hud/settings/settings_controls.cpp
// Input and the open list for the settings panel's slider and dropdown (see
// settings_controls.h). The layout draws both controls; this file answers the
// mouse and, for a dropdown, draws the list last, over everything else.
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../gamepad_widget.h"
#include "../pitboard_hud.h"
#include "../tacho_widget.h"
#include "../speedo_widget.h"
#include "../../core/asset_manager.h"
#include "../../core/hud_manager.h"
#include "../../core/color_config.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/font_config.h"
#include "../../diagnostics/logger.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstring>

using namespace PluginConstants;

// A left press. Returns true when it was this file's: a press on a slider starts
// a drag, and any press while a list is open that misses the list closes it and
// goes no further (the click under the list was not aimed at what it covers).
// A press on the list's own entries or box falls through, to fire on release.
bool SettingsHud::handleControlPress(int regionIndex, float cursorX) {
    const ClickRegion* region = (regionIndex >= 0) ? &m_clickRegions[regionIndex] : nullptr;
    if (m_dropdown.open >= 0) {
        if (region && (region->type == ClickRegion::DROPDOWN_OPTION ||
                       region->type == ClickRegion::DROPDOWN)) {
            return false;
        }
        m_dropdown.open = -1;
        setDataDirty();
        return true;
    }
    if (!region || region->type != ClickRegion::SLIDER) return false;
    m_sliderDrag.index = region->steppedIndex;
    // Regions are in build space; the cursor is on screen, where a dragged panel
    // sits at its offset (isPointInRect applies the same).
    float trackX = region->x, trackY = region->y;
    applyOffset(trackX, trackY);
    m_sliderDrag.trackX = trackX;
    m_sliderDrag.trackW = region->width;
    updateSliderDrag(cursorX, true);
    return true;
}

// While the button is held, the value follows the cursor along the track it was
// grabbed on, past either end too (clamped). Release ends the drag and saves
// once, as a held stepper does.
void SettingsHud::updateSliderDrag(float cursorX, bool pressed) {
    if (m_sliderDrag.index < 0) return;
    if (!pressed) {
        endSliderDrag();
        markSettingsDirty();
        return;
    }
    if (m_sliderDrag.index >= static_cast<int>(m_sliders.size()) || m_sliderDrag.trackW <= 0.0f) {
        endSliderDrag();   // with the track lost (zero width), a deferred value still lands
        return;
    }
    const SliderControl& control = m_sliders[m_sliderDrag.index];
    if (control.valid && !control.valid()) {
        // The target changed under the open menu (SteppedControl::valid).
        m_sliderDrag.index = -1;
        setDataDirty();
        return;
    }
    if (!control.get || !control.set) return;
    const float value = control.valueAt((cursorX - m_sliderDrag.trackX) / m_sliderDrag.trackW);
    if (std::fabs(value - control.get()) < 1e-6f) return;   // same step: nothing to rebuild
    control.set(value);
    // A deferring slider's postStep waits for the value to land (endSliderDrag).
    if (control.postStep && !control.onRelease) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
    setDataDirty();
}

// The drag is over (released, or the panel closing under it): a slider that
// defers its value (SteppedControl::dragSet) applies it now.
void SettingsHud::endSliderDrag() {
    const int index = m_sliderDrag.index;
    m_sliderDrag.index = -1;
    if (index < 0 || index >= static_cast<int>(m_sliders.size())) return;
    const SliderControl& control = m_sliders[index];
    if (!control.onRelease) return;
    control.onRelease();
    if (control.postStep) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
    setDataDirty();
    markSettingsDirty();
}

// Released over a dropdown's box (open or close its list) or one of its entries
// (pick it). A SLIDER region never gets here from the mouse -- its press is
// consumed above -- so it has nothing to do on release.
void SettingsHud::handleControlRegion(const ClickRegion& region) {
    if (region.type == ClickRegion::DROPDOWN) {
        m_dropdown.open = (m_dropdown.open == region.cycleIndex) ? -1 : region.cycleIndex;
        m_dropdown.tab = m_activeTab;
        setDataDirty();
        return;
    }
    if (region.type != ClickRegion::DROPDOWN_OPTION) return;
    m_dropdown.open = -1;
    setDataDirty();
    if (region.cycleIndex < 0 || region.cycleIndex >= static_cast<int>(m_cycleControls.size())) {
        DEBUG_WARN_F("Dropdown entry with invalid descriptor index: %d", region.cycleIndex);
        return;
    }
    const CycleControl& control = m_cycleControls[region.cycleIndex];
    const int entry = static_cast<int>(region.flagBit);
    if (!control.set || entry < 0 || entry >= control.count) return;
    control.set(entry);
    if (control.postStep) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
}

// The open list, drawn LAST so its quads cover the rows beneath. The game draws
// every quad before any string, though, so a quad cannot hide text: the strings
// the list covers are blanked instead (blanked, not erased -- the title's string
// index must stay put).
//
// PLACEMENT: below the box when the list fits there, else above it; a list too
// long for either side wraps into columns on the roomier side (a font or icon
// list runs to dozens), shifted left if the columns would leave the panel.
//
// ITS ENTRIES ARE APPENDED, so every region index the layout handed out this
// rebuild stays what it was -- the hover index is compared against those, and
// with the entries inserted in front, hovering an entry lit the sidebar row with
// the same index. They win over the rows they cover through firstRegion, which
// findClickRegionAt and the hover scan search first.
void SettingsHud::buildDropdownPopup(float panelLeft, float panelTop, float panelRight,
                                     float panelBottom) {
    m_dropdown.firstRegion = -1;
    if (m_dropdown.open < 0) return;
    if (!m_dropdown.anchored || m_dropdown.tab != m_activeTab ||
        m_dropdown.open >= static_cast<int>(m_cycleControls.size())) {
        m_dropdown.open = -1;
        return;
    }
    const CycleControl& control = m_cycleControls[m_dropdown.open];
    const int count = control.count;
    if (!control.nameOf || count <= 0) { m_dropdown.open = -1; return; }

    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(count));
    size_t longest = 0;
    for (int i = 0; i < count; ++i) {
        names.push_back(control.nameOf(i));
        longest = std::max(longest, names.back().size());
    }

    const float rowH = m_dropdown.rowH;
    const float fontSize = m_dropdown.fontSize;
    const float pad = m_dropdown.textX - m_dropdown.x;
    const bool iconGrid = static_cast<bool>(control.spriteOf);
    // A colour list leads each name with its swatch: a character and a half.
    const float swatchW = control.swatchOf
        ? PluginUtils::calculateMonospaceTextWidth(1, fontSize) * 1.5f : 0.0f;

    // Entry geometry. A text list is one column of names, wrapped into more
    // columns on the roomier side when it fits neither below nor above the box.
    // An icon list (a few hundred icons) is a grid of square cells as wide as the
    // panel allows, with one caption row under it naming the hovered icon.
    float cellW = 0.0f, cellH = rowH, captionH = 0.0f;
    int perCol = count;   // text: entries per column; grid: unused
    int cols = 1, rows = count;
    bool below = true;
    const float spaceBelow = panelBottom - (m_dropdown.y + rowH);
    const float spaceAbove = m_dropdown.y - panelTop;
    if (iconGrid) {
        cellH = rowH * 1.2f;
        cellW = cellH / UI_ASPECT_RATIO;
        captionH = rowH;
        cols = std::max(1, std::min(count, static_cast<int>((panelRight - panelLeft - pad * 2.0f) / cellW)));
        rows = (count + cols - 1) / cols;
        const float gridH = cellH * static_cast<float>(rows) + captionH;
        below = gridH <= spaceBelow || spaceBelow >= spaceAbove;
    } else {
        cellW = std::max(m_dropdown.w,
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
    float x = m_dropdown.x;
    if (x + w > panelRight) x = std::max(panelLeft, panelRight - w);
    const float y = below ? m_dropdown.y + rowH : m_dropdown.y - h;

    // Strings are stored offset; compare against the list's offset rect.
    float ox = x, oy = y;
    applyOffset(ox, oy);
    for (SPluginString_t& str : m_strings) {
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

    ColorConfig& colors = ColorConfig::getInstance();
    const float edge = rowH * 0.06f;
    const int current = control.get ? control.get() : -1;
    auto quad = [this](float qx, float qy, float qw, float qh, unsigned long color) {
        SPluginQuad_t q;
        applyOffset(qx, qy);
        setQuadPositions(q, qx, qy, qw, qh);
        q.m_iSprite = SpriteIndex::SOLID_COLOR;   // solid-quad-exempt: the open list is a flat box over the rows, drawn the same with or without a theme
        q.m_ulColor = color;
        m_quads.push_back(q);
    };
    // Outline, then an opaque body inside it.
    quad(x - edge, y - edge, w + edge * 2.0f, h + edge * 2.0f, colors.getAccent());
    quad(x, y, w, h, PluginUtils::applyOpacity(colors.getBackground(), 1.0f));

    m_dropdown.firstRegion = static_cast<int>(m_clickRegions.size());
    int hovered = -1;
    for (int i = 0; i < count; ++i) {
        const float ex = iconGrid ? x + pad + cellW * static_cast<float>(i % cols)
                                  : x + cellW * static_cast<float>(i / perCol);
        const float ey = iconGrid ? y + cellH * static_cast<float>(i / cols)
                                  : y + rowH * static_cast<float>(i % perCol);
        const int regionIndex = static_cast<int>(m_clickRegions.size());
        if (m_hoveredRegionIndex == regionIndex) hovered = i;
        if (i == current) {
            quad(ex, ey, cellW, cellH, PluginUtils::applyOpacity(colors.getAccent(), 0.25f));
        } else if (hovered == i) {
            quad(ex, ey, cellW, cellH, PluginUtils::applyOpacity(colors.getAccent(), 0.12f));
        }
        const unsigned long ink = i == current ? colors.getPrimary() : colors.getSecondary();
        if (iconGrid) {
            const int sprite = control.spriteOf(i);
            if (sprite > 0) {
                addIcon(ex + cellW * 0.5f, ey + cellH * 0.5f, sprite, ink, cellH * 0.7f);
            } else {
                addString("-", ex + cellW * 0.5f, ey + (cellH - rowH) * 0.5f, Justify::CENTER,
                    Fonts::getNormal(), ink, fontSize);
            }
        } else {
            if (control.swatchOf) {
                quad(ex + pad, ey + rowH * 0.22f, swatchW * 0.66f, rowH * 0.56f, control.swatchOf(i));
            }
            // A font list previews each face; a colour list writes each name in
            // its colour, unless that colour would vanish on the list's body.
            const int face = control.fontOf ? control.fontOf(i) : 0;
            unsigned long nameInk = ink;
            if (control.swatchOf) {
                const unsigned long c = control.swatchOf(i);
                if (!PluginUtils::isColorDark(c)) nameInk = c;
            }
            addString(names[static_cast<size_t>(i)].c_str(), ex + pad + swatchW, ey, Justify::LEFT,
                face > 0 ? face : Fonts::getNormal(), nameInk, fontSize);
        }
        ClickRegion entry(ex, ey, cellW, cellH, ClickRegion::DROPDOWN_OPTION, nullptr,
                          static_cast<uint32_t>(i));
        entry.cycleIndex = m_dropdown.open;
        m_clickRegions.push_back(entry);
    }
    if (iconGrid) {
        // The caption names what a click would pick: the hovered icon, else the current one.
        const int named = hovered >= 0 ? hovered : current;
        if (named >= 0 && named < count) {
            addString(names[static_cast<size_t>(named)].c_str(), x + pad, y + h - captionH,
                Justify::LEFT, Fonts::getNormal(), colors.getPrimary(), fontSize);
        }
    }
}

// The shared stepper and cycle handlers (STEPPED_* / CYCLE_* regions), beside
// the slider and dropdown that set the same descriptors directly.
// Shared handler for STEPPED_UP/STEPPED_DOWN: apply the registered descriptor's
// step (with the usual hold-to-repeat acceleration) and mark the target HUD +
// this menu dirty - the exact archetype the per-control enum pairs used to
// hand-roll. Only calls setDataDirty (never rebuildRenderData) so the region
// reference and descriptor vector stay valid for the caller.
void SettingsHud::applySteppedControl(const ClickRegion& region, bool increase) {
    if (region.steppedIndex < 0 ||
        region.steppedIndex >= static_cast<int>(m_steppedControls.size())) {
        DEBUG_WARN_F("Stepped region with invalid descriptor index: %d", region.steppedIndex);
        return;
    }
    const SteppedControl& control = m_steppedControls[region.steppedIndex];
    if (control.valid && !control.valid()) {
        // The state this descriptor bound to at layout time is no longer the
        // active target (e.g. the per-bike rumble profile changed under an open
        // menu - the pointers would edit the PREVIOUS bike's profile). Swallow
        // the click and mark the layout dirty so the next frame rebuilds the
        // controls against the right target.
        DEBUG_INFO("Stepped control target changed since layout; click ignored");
        setDataDirty();
        return;
    }
    switch (control.kind) {
        case SteppedControl::Kind::WRAP_INT:
            if (control.intValue) {
                *control.intValue = applyAcceleratedWrap(
                    *control.intValue, control.step, control.lo, control.hi, increase);
            }
            break;
        case SteppedControl::Kind::CLAMP_INT:
            if (control.intValue) {
                *control.intValue = applyAcceleratedClamp(
                    *control.intValue, control.step, control.lo, control.hi, increase);
            }
            break;
        case SteppedControl::Kind::FIXED_INT:
            if (control.intValue) {
                // Fixed integer step with deliberately NO hold acceleration (matches
                // the old plain ++/-- count handlers), clamped to [lo, hi].
                int next = *control.intValue + (increase ? control.step : -control.step);
                if (next < control.lo) next = control.lo;
                if (next > control.hi) next = control.hi;
                *control.intValue = next;
            }
            break;
        case SteppedControl::Kind::STEP_FLOAT:
            if (control.floatValue) {
                float next = applyAcceleratedStep(*control.floatValue, control.fstep, increase);
                // Clamp toward the pressed direction only (matches the old
                // per-control handlers, which bounded UP at max and DOWN at min).
                if (increase) { if (next > control.fhi) next = control.fhi; }
                else          { if (next < control.flo) next = control.flo; }
                *control.floatValue = next;
            }
            break;
        case SteppedControl::Kind::PERCENT_FLOAT:
            if (control.floatValue) {
                // Verbatim rumble-strength semantics (the old adjustEffectStrength):
                // accelerated 1% step, clamp, THEN round to hundredths. Deliberately
                // not STEP_FLOAT - its snap-to-accelerated-grid would change the
                // value sequences under hold acceleration.
                float step = control.fstep * getHoldStepMultiplier();
                float value = *control.floatValue;
                if (increase) {
                    value = std::round(std::min(value + step, control.fhi) * 100.0f) / 100.0f;
                } else {
                    value = std::round(std::max(value - step, control.flo) * 100.0f) / 100.0f;
                }
                *control.floatValue = value;
            }
            break;
        case SteppedControl::Kind::ACCESSOR:
            if (control.get && control.set && control.fstep > 0.0f) {
                float next = control.get() + (increase ? 1.0f : -1.0f) * control.fstep *
                    static_cast<float>(getHoldStepMultiplier());
                next = std::clamp(next, control.flo, control.fhi);
                control.set(std::clamp(std::round(next / control.fstep) * control.fstep,
                                       control.flo, control.fhi));
            }
            break;
        case SteppedControl::Kind::FIXED_FLOAT:
            if (control.floatValue) {
                // Fixed step with deliberately NO hold acceleration (matches the old
                // rumble Min/Max input steppers). The lower bound can be linked to a
                // live value (a rumble effect's Max input clamps at its current Min).
                if (increase) {
                    *control.floatValue = std::min(*control.floatValue + control.fstep, control.fhi);
                } else {
                    float lo = control.loLink ? *control.loLink : control.flo;
                    *control.floatValue = std::max(*control.floatValue - control.fstep, lo);
                }
            }
            break;
    }
    if (control.postStep) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
    setDataDirty();
}

// Shared handler for CYCLE_UP/CYCLE_DOWN: step the registered descriptor's
// 0-based state index forward/backward mod count and mark the target HUD +
// this menu dirty - the archetype the per-control enum pairs (label/color/mode
// cycles) used to hand-roll. Deliberately NO hold acceleration: a held cycle
// repeats at the base cadence with step 1, exactly like the old handlers
// (which ignored the hold tier). Only calls setDataDirty (never
// rebuildRenderData) so the region reference and descriptor vector stay valid
// for the caller.
void SettingsHud::applyCycleControl(const ClickRegion& region, bool forward) {
    if (region.cycleIndex < 0 ||
        region.cycleIndex >= static_cast<int>(m_cycleControls.size())) {
        DEBUG_WARN_F("Cycle region with invalid descriptor index: %d", region.cycleIndex);
        return;
    }
    const CycleControl& control = m_cycleControls[region.cycleIndex];
    if (control.step) {
        control.step(forward);
    } else if (control.get && control.set && control.count > 0) {
        const int current = control.get();
        const int next = forward ? (current + 1) % control.count
                                 : (current + control.count - 1) % control.count;
        control.set(next);
    }
    if (control.postStep) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
    setDataDirty();
}

// ============================================================================
// The lists a row picks from. Each maps the list's 0-based position to the
// stored value and back, so the arrows step it (applyCycleControl) and the
// dropdown sets it (handleControlRegion) through the same descriptor.
// ============================================================================

// A cycle over a list of stored VALUES (variant numbers, shape indices): the
// state is the value's position in the list, a value not in it reads as the first.
static SettingsHud::CycleControl valueListCycle(std::vector<int> values, std::function<int()> get,
                                                std::function<void(int)> set,
                                                std::function<std::string(int)> nameOfValue,
                                                BaseHud* dirtyHud,
                                                std::function<int(int)> spriteOfValue = nullptr) {
    auto list = std::make_shared<std::vector<int>>(std::move(values));
    SettingsHud::CycleControl c;
    c.count = static_cast<int>(list->size());
    c.get = [list, get]() {
        const int v = get();
        for (size_t i = 0; i < list->size(); ++i) if ((*list)[i] == v) return static_cast<int>(i);
        return 0;
    };
    c.set = [list, set](int i) { set((*list)[static_cast<size_t>(i)]); };
    c.nameOf = [list, nameOfValue](int i) { return nameOfValue((*list)[static_cast<size_t>(i)]); };
    if (spriteOfValue) {
        c.spriteOf = [list, spriteOfValue](int i) { return spriteOfValue((*list)[static_cast<size_t>(i)]); };
    }
    c.dirtyHud = dirtyHud;
    return c;
}

SettingsHud::CycleControl textureVariantCycle(std::vector<int> variants, std::function<int()> get,
                                              std::function<void(int)> set, BaseHud* dirtyHud) {
    variants.insert(variants.begin(), 0);   // Off first
    return valueListCycle(std::move(variants), std::move(get), std::move(set),
        [](int v) { return v <= 0 ? std::string("Off") : std::to_string(v); }, dirtyHud);
}

SettingsHud::CycleControl paletteCycle(std::function<unsigned long()> get,
                                       std::function<void(unsigned long)> set, BaseHud* dirtyHud) {
    SettingsHud::CycleControl c;
    c.count = static_cast<int>(ColorPalette::ALL_COLORS.size());
    c.get = [get]() { return std::max(0, ColorPalette::getColorIndex(get())); };
    c.set = [set](int i) { set(ColorPalette::ALL_COLORS[static_cast<size_t>(i)]); };
    c.nameOf = [](int i) {
        return std::string(ColorPalette::getColorName(ColorPalette::ALL_COLORS[static_cast<size_t>(i)]));
    };
    c.swatchOf = [](int i) { return ColorPalette::ALL_COLORS[static_cast<size_t>(i)]; };
    c.dirtyHud = dirtyHud;
    return c;
}

SettingsHud::CycleControl iconCycle(std::function<int()> get, std::function<void(int)> set,
                                    BaseHud* dirtyHud, bool allowOff, const char* zeroIcon) {
    // The order the arrows step: Off (when allowed), then every icon but the HUD
    // identity ones -- stepShapeIndexSkippingHud's own walk, so the two agree.
    const AssetManager& assets = AssetManager::getInstance();
    const int iconCount = static_cast<int>(assets.getIconCount());
    std::vector<int> shapes;
    shapes.reserve(static_cast<size_t>(iconCount) + 1);
    const int first = allowOff ? 0 : assets.stepShapeIndexSkippingHud(iconCount, true, false);
    int shape = first;
    do {
        shapes.push_back(shape);
        shape = assets.stepShapeIndexSkippingHud(shape, true, allowOff);
    } while (shape != first && static_cast<int>(shapes.size()) <= iconCount);
    const int zeroSprite = zeroIcon ? assets.getIconSpriteIndex(zeroIcon) : 0;
    return valueListCycle(std::move(shapes), std::move(get), std::move(set),
        [zeroSprite](int v) {
            if (v <= 0 && zeroSprite > 0) {
                return AssetManager::getInstance().getIconDisplayName(zeroSprite);
            }
            return getShapeDisplayName(v);
        }, dirtyHud,
        [zeroSprite](int v) { return v > 0 ? AssetManager::getInstance().iconSpriteForShape(v) : zeroSprite; });
}

// A HUD's background texture: Off, then its variants -- except where the artwork
// is the widget (m_textureRequired), which picks among the variants alone.
SettingsHud::CycleControl SettingsHud::textureCycle(BaseHud* hud) {
    auto get = [hud]() { return hud->getTextureVariant(); };
    auto set = [hud](int v) { hud->setTextureVariant(v); };
    if (!hud->m_textureRequired) {
        return textureVariantCycle(hud->getAvailableTextureVariants(), get, set, hud);
    }
    return valueListCycle(hud->getAvailableTextureVariants(), get, set,
        [](int v) { return std::to_string(v); }, hud);
}

// A HUD's panel theme: Default (the global one), None, then each installed theme.
// A stored name that is no longer installed reads as Default, which is also what
// it draws as. A theme RESIZES the panel, so one parked against an edge can grow
// off the display: the pick asks for position validation, as the global cycle does.
SettingsHud::CycleControl SettingsHud::themeOverrideCycle(BaseHud* hud) {
    CycleControl c;
    c.count = static_cast<int>(AssetManager::getInstance().getThemes().size()) + 2;
    c.get = [hud]() {
        const std::string& cur = hud->getThemeOverride();
        if (cur == BaseHud::THEME_NONE) return 1;
        const auto& themes = AssetManager::getInstance().getThemes();
        for (size_t i = 0; i < themes.size(); ++i) {
            if (!cur.empty() && themes[i].name == cur) return static_cast<int>(i) + 2;
        }
        return 0;
    };
    c.set = [hud](int i) {
        const auto& themes = AssetManager::getInstance().getThemes();
        if (i <= 0) hud->setThemeOverride("");
        else if (i == 1) hud->setThemeOverride(BaseHud::THEME_NONE);
        else hud->setThemeOverride(themes[static_cast<size_t>(i) - 2].name);
    };
    c.nameOf = [](int i) {
        if (i <= 0) return std::string("Default");
        if (i == 1) return std::string("None");
        return AssetManager::getInstance().getThemes()[static_cast<size_t>(i) - 2].displayName;
    };
    c.postStep = []() { HudManager::getInstance().requestPositionValidation(); };
    c.dirtyHud = hud;
    return c;
}

// A pack HUD's Texture row: the installed packs, by name. NO Off entry: the pack
// artwork IS the widget (BaseHud::m_textureRequired), and without it a pad's
// buttons or a board's rows hang on an empty panel. The current entry is the pack
// actually IN USE, not the stored name: an uninstalled name draws the shipped
// default, and the list has to start from what is on screen. Pads and boards
// differ in aspect, so a pick asks for position validation; a dial is drawn as a
// circle at the widget's own size, so a gauge pick cannot move its panel.
template <typename PackT, typename HudT>
static SettingsHud::CycleControl packListCycle(const std::vector<PackT>& packs, HudT* hud,
                                               void (HudT::*setPack)(const std::string&),
                                               bool revalidate) {
    SettingsHud::CycleControl c;
    c.count = static_cast<int>(packs.size());
    c.get = [&packs, hud]() {
        const PackT* active = hud->activePack();
        for (size_t i = 0; i < packs.size(); ++i) {
            if (active && packs[i].name == active->name) return static_cast<int>(i);
        }
        return 0;
    };
    c.set = [&packs, hud, setPack](int i) { (hud->*setPack)(packs[static_cast<size_t>(i)].name); };
    c.nameOf = [&packs](int i) { return packs[static_cast<size_t>(i)].displayName; };
    if (revalidate) c.postStep = []() { HudManager::getInstance().requestPositionValidation(); };
    c.dirtyHud = hud;
    return c;
}

SettingsHud::CycleControl SettingsHud::packCycle(BaseHud* hud) {
    const AssetManager& assets = AssetManager::getInstance();
    CycleControl c;
    if (auto* pad = dynamic_cast<GamepadWidget*>(hud)) {
        c = packListCycle(assets.getGamepads(), pad, &GamepadWidget::setGamepadPack, true);
    } else if (auto* board = dynamic_cast<PitboardHud*>(hud)) {
        c = packListCycle(assets.getPitboards(), board, &PitboardHud::setPitboardPack, true);
    } else if (auto* tacho = dynamic_cast<TachoWidget*>(hud)) {
        c = packListCycle(assets.getGauges(), tacho, &TachoWidget::setGaugesPack, false);
    } else if (auto* speedo = dynamic_cast<SpeedoWidget*>(hud)) {
        c = packListCycle(assets.getGauges(), speedo, &SpeedoWidget::setGaugesPack, false);
    }
    c.repeat = false;   // a pack step loads its art
    return c;
}

// One step of a descriptor, as its arrows take it (wrapping).
void SettingsHud::stepCycle(const CycleControl& c, bool forward) {
    if (!c.get || !c.set || c.count <= 0) return;
    c.set(((c.get() + (forward ? 1 : -1)) % c.count + c.count) % c.count);
    if (c.postStep) c.postStep();
    if (c.dirtyHud) c.dirtyHud->setDataDirty();
}

void SettingsHud::cycleGamepadPack(bool forward) {
    stepCycle(packCycle(&HudManager::getInstance().getGamepadWidget()), forward);
}

void SettingsHud::cyclePitboardPack(bool forward) {
    stepCycle(packCycle(&HudManager::getInstance().getPitboardHud()), forward);
}
