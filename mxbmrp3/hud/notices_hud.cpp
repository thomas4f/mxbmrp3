// ============================================================================
// hud/notices_hud.cpp
// Notices HUD - displays warnings and PB notifications
// Shows centered notices above the timing HUD area
// ============================================================================
#include "notices_hud.h"
#include "notice_tracker.h"
#include "rider_flag_icons.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "../core/ui_config.h"
#include "center_stack.h"

using namespace PluginConstants;

// Center display positioning constants (fixed center-screen layout)
namespace {
    // The notice grows UP, with its BOTTOM one row-gap above this divider -- which is
    // the same line the Timing box starts on. Cell 11; see hud/center_stack.h for the
    // stack's specification and why this is computed rather than written out.
    // Stable regardless of the Timing HUD's height (it's below and grows away).
    // Shared with the Timing HUD so the two centered top-stack panels are the same width.
    constexpr int NOTICE_WIDTH_CHARS = WidgetDimensions::CENTER_STACK_WIDTH_CHARS;

}

NoticesHud::NoticesHud()
{
    // TITLE RESTORED, TEMPORARILY. This panel was one of the three the caption was taken
    // from (see BaseHud::m_titleSupported for the twelve that keep it off). It is back so
    // the reason the caption was unwanted can be shown rather than described -- nothing
    // else about this HUD reverted with it: the panel, its body card, the coloured
    // block's outset and the stack spacing are all as the last few commits left them.
    // A PANEL AND A BODY CARD, like both siblings in the centre stack, and set here
    // like theirs rather than derived per rebuild. It used to be `m_bContentCard =
    // m_bShowTitle`: untitled, this widget drew NO panel at all -- the coloured slab
    // was the whole thing -- and switching the caption on was what promoted it to a
    // real panel. With the caption gone that left the slab as the only reachable
    // shape, a bare button-coloured blob beside a framed Gap Bar. Reported as "what
    // has happened to the content band of the notices, shouldn't it look like the gap
    // bar".
    //
    // It costs NO height, which is what made the old gating look necessary and is not:
    // the slab already paid a padding at each end when it was the widget, and the panel
    // pays exactly that now, so the box is no taller than the widget was. The slab
    // moves inside the card instead of being the box.
    m_bContentCard = true;
    // One-time setup
    setDraggable(true);
    m_quads.reserve(2);   // the slab and its glow (themed slices grow it once)
    m_strings.reserve(1);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("notices_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool NoticesHud::handlesDataType(DataChangeType /*dataType*/) const {
    return false;  // We poll PluginData directly in update()
}

void NoticesHud::update() {
    // The notice state is NoticeTracker's, stepped once per frame by HudManager
    // before any HUD updates (including the drain of expired notices while nothing
    // shows them). This panel only redraws when the notice it shows changes.
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    const uint32_t revision = NoticeTracker::getInstance().revision();
    if (revision != m_renderedRevision) {
        m_renderedRevision = revision;
        setDataDirty();
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void NoticesHud::rebuildLayout() {
    // FULL REBUILD, like TimingHud and GapBarHud beside it.
    //
    // This used to be a hand-written fast path that rewrote m_quads[0] as the notice
    // slab. That index is only the slab while this widget draws nothing else -- with
    // a title it is the panel's first frame slice, so the fast path would drag the
    // frame and leave the slab behind. The path had already cost two desync bugs
    // (its own comments recorded both: a 1-quad vector written with 9 slices, and a
    // rewrite using the wrong slice set), and it duplicated every dimension in
    // rebuildRenderData to do it.
    //
    // Cheap enough to be the right trade: at most one notice is ever on screen, so a
    // rebuild is a background, a caption and one slab.
    rebuildRenderData();
}

// One notice background. A notice is a coloured slab whose COLOUR is the message
// (red = wrong way, blue = blue flag), which is exactly what the theme's button
// slices are for -- they are white+alpha and always take the caller's colour, so a
// baked-colour theme cannot multiply the meaning away. The plain quad is the
// fallback when the theme has no button set.
//
// Every branch below goes through here so the quad COUNT is uniform: the layout
// fast path rewrites this span by index, and a branch that pushed its own single
// quad would desync it.
// A notice IS a button shape: a small coloured rectangle whose colour is state.
// This reimplemented addButtonQuad exactly -- themed branch, offset, sprite, colour
// -- which is the duplication that helper was written to end.
void NoticesHud::addNoticeBackground(float x, float y, float w, float h, unsigned long color) {
    // THE THEME'S BUTTON SLICES, and the Gap Bar's fill goes through the same call.
    // Both are a coloured block whose colour is the reading, so a theme that shapes its
    // buttons shapes these too -- one decision for the pair rather than one panel
    // borrowing the art and its neighbour drawing a flat rectangle.
    //
    // The rect is the card's INTERIOR, which is what makes the borrowed shape sit
    // flush: a button bevel does not colour its own edge slices, so the visible colour
    // is inset from this rect by the theme's [button] size. That inset is the theme's
    // to choose -- a theme wanting the colour edge to edge ships flat button art.
    //
    // NOT opaque: a notice borrows a button's shape but is not a control. These ship at
    // 10% background opacity -- a whisper over the track -- and the button rule (a thing
    // you click stays legible) would make every one a solid box.
    addButtonQuad(x, y, w, h, color, /*opaque=*/false);
}

void NoticesHud::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    // The notice up right now, already resolved through the priority ladder and the
    // enabled mask (NoticeTracker::resolveCurrent):
    // WRONG WAY > HAZARD AHEAD > BLUE FLAG > LAPPER AHEAD > OVERTIME > ALL-TIME PB >
    // FASTEST LAP > SESSION PB > SEGMENT > FINISHED > LAST LAP > DEFAULT SETUP
    const NoticeTracker::Notice& notice = NoticeTracker::getInstance().current();
    using Kind = NoticeTracker::Kind;

    // Only render if there's something to show...
    //
    // ...or the Notices tab is open (isPreviewing), in which case a stand-in slab
    // draws in its place. A notice is up for a second or two an hour, which is no
    // time to drag the panel anywhere.
    //
    // GATED ON HAVING ANY NOTICE SWITCHED ON, like every other element's preview
    // is gated on its own content switch: with the lot turned off this panel can
    // never show anything, so standing in for it would be arguing with the player.
    // (The segment notice is not in the mask -- it is self-gated by the segment
    // hotkey -- so it is asked for separately.)
    const bool segmentUp = notice.kind == Kind::SegmentAdded || notice.kind == Kind::SegmentRemoved;
    const bool preview = isPreviewing() && (m_enabledNotices != 0 || segmentUp);
    if (notice.kind == Kind::None && !preview) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // BOX-MODEL: one section whose content is the coloured slab's row. The slab
    // IS the section card's border box when one is drawn — the "block meets the
    // card's outer edge on all four sides" rule, now by construction rather than
    // by outsetting the interior back over the border (the old
    // contentCardSpanY + drawnCardBorder* dance). Unthemed, the card box
    // degenerates to the content box and the slab is the row, as before.
    //
    // The centre-stack width rides as the panel MINIMUM (see CenterStack::boxWidth);
    // its own stack line is measured DOWN and snapped, as before.
    PanelWant want;
    wantCenterStackWidth(want, dim);   // stack width as the minimum, nothing competing
    want.sectionH = { bigValueRowHeight(dim) };
    want.captionW = planTitleWidth(dim, "Notices", TitleTier::Large);
    want.tier = TitleTier::Large;
    // The message slab IS this panel: unthemed, it takes the cell of panel padding
    // rather than floating inside it. Same outer rect either way; with a theme on,
    // the frame keeps its ring. See PanelWant::contentFillsPanel.
    want.contentFillsPanel = true;
    PanelPlan& plan = planPanel(dim, want);

    const float panelW = plan.width();
    // CENTRE-ANCHORED: offsetX is this panel's CENTRE, so a width change recentres
    // instead of walking an edge. It stored a DELTA from the computed centre until
    // settings v7; the migration adds the anchor in.
    const float panelX = centerAnchoredPanelLeft(panelW);
    // ZERO, like GapBar's boxTop and Timing's START_Y: the panel anchors at the
    // offset and the stored offset IS its top. This used to bake noticesBoxTop() in
    // and treat the offset as a delta from it, which meant the same offsetY in the
    // INI put this panel somewhere else than the two it stacks with -- invisible
    // while the three had different defaults, and wrong the moment they share one.
    const float panelY = snapEdgeY(0.0f);
    addPlanBackground(plan, panelX, panelY);
    addPlanTitle(plan, "Notices",
                 this->getColor(ColorSlot::PRIMARY));

    // The SLAB (the card's drawn box; = the row unthemed), via the shared
    // accessors -- the message centres on it both ways now. Its width used to be
    // the card minus the LEFT content inset mirrored onto both sides, the same
    // idiom that pulled Fuel's value inward (see PanelPlan::contentRight).
    const float slabX = plan.sectionBoxX();
    const float slabW = plan.sectionBoxW();
    const float slabY = plan.sectionBoxY();
    const float slabH = plan.sectionBoxH();

    // INK-centred in THE SLAB, like the Timing panel's big time below it -- the message
    // is drawn ON the slab, so the slab is the box it has to look centred in. It used
    // to centre in the row, which is the same place until [content] border is
    // asymmetric and a cell high when it is (PanelPlan::sectionBoxY).
    float noticeY = inkCenteredY(slabY, slabH, dim.fontSizeLarge);
    float noticeCenterX = slabX + slabW / 2.0f;

    // The slab's colour is the message (slabColors). The preview stand-in says one
    // word: this slot carries WRONG WAY and HAZARD AHEAD at a glance, so a sentence
    // here reads as a notice rather than as the absence of one.
    unsigned long fill = 0, ink = 0;
    slabColors(notice.kind, m_fBackgroundOpacity, fill, ink);
    if (glows(notice.kind)) addGlow(slabX, slabY, slabW, slabH, slabH * GLOW_SPREAD, fill, glowOpacity(notice.kind));
    addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(fill, m_fBackgroundOpacity));
    addString(notice.kind == Kind::None ? "PREVIEW" : NoticeTracker::caption(notice, slabW, dim.fontSizeLarge, this->getFont(FontCategory::TITLE)),
              noticeCenterX, noticeY, Justify::CENTER,
              this->getFont(FontCategory::TITLE), ink, dim.fontSizeLarge);

    setBounds(panelX, panelY, panelX + panelW, panelY + plan.height());
}

// WRONG WAY and RIDER ONCOMING are the critical ones: full strength; the rest
// at the calm share.
float NoticesHud::glowOpacity(NoticeTracker::Kind kind) {
    using Kind = NoticeTracker::Kind;
    const bool critical = kind == Kind::WrongWay || kind == Kind::RiderOncoming;
    return GLOW_OPACITY * (critical ? 1.0f : UiConfig::GLOW_CALM);
}

bool NoticesHud::glows(NoticeTracker::Kind kind) {
    using Kind = NoticeTracker::Kind;
    return kind == Kind::WrongWay || kind == Kind::HazardAhead ||
           kind == Kind::RiderOncoming || kind == Kind::BlueFlag;
}

// The slab's colour is the message (red = wrong way, blue = blue flag). The caption
// takes captionOnSlabColor where the slab is a strong colour, and plain PRIMARY over
// the neutral BACKGROUND slab.
void NoticesHud::slabColors(NoticeTracker::Kind kind, float opacity, unsigned long& fill,
                            unsigned long& ink) const {
    using Kind = NoticeTracker::Kind;
    switch (kind) {
        case Kind::WrongWay:      fill = getColor(ColorSlot::NEGATIVE); break;
        // Same slot, same WARNING colour -- only the wording separates a rider
        // coming at you from one stopped on the track.
        case Kind::HazardAhead:
        case Kind::RiderOncoming: fill = getColor(ColorSlot::WARNING); break;
        case Kind::BlueFlag:      fill = FlagColors::BLUE; break;
        // Neutral/yellow -- informational caution; distinct from the orange
        // WARNING that hazards use.
        case Kind::LapperAhead:   fill = getColor(ColorSlot::NEUTRAL); break;
        case Kind::AllTimePB:
        case Kind::FastestLap:
        case Kind::SessionPB:     fill = getColor(ColorSlot::POSITIVE); break;
        case Kind::SegmentAdded:
            // Adding a point = positive (green); removing = neutral.
            fill = ink = getColor(ColorSlot::POSITIVE);
            return;
        case Kind::DefaultSetup:  fill = getColor(ColorSlot::WARNING); break;
        case Kind::Overtime:
        case Kind::TrackChanged:
        case Kind::SegmentRemoved:
        case Kind::Finished:
        case Kind::LastLap:
            fill = getColor(ColorSlot::BACKGROUND);
            ink = getColor(ColorSlot::PRIMARY);
            return;
        case Kind::None:
        default:
            // The preview stand-in (a real notice always wins: it only draws on a
            // frame where nothing else would have). Accent rather than one of the
            // notice colours, so it does not read as a warning that is actually up.
            fill = getColor(ColorSlot::ACCENT);
            break;
    }
    ink = captionOnSlabColor(fill, opacity);
}

void NoticesHud::resetToDefaults() {
    // Off by default since 1.33: the Dash shows each notice (Notice focus, on by
    // default). Fresh installs only -- an upgraded INI keeps the panel on.
    m_bVisible = false;
    // Off by DEFAULT, not unavailable -- the toggle is in the Notices tab. Switching
    // it on promotes this widget from a bare coloured slab to a real panel with the
    // slab as its content; see rebuildRenderData for why the panel keeps the slab's
    // outer width instead of growing around it.
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    // The spot under the Dash that the Gap Bar and Timing share: offsetX is the
    // CENTRE (all four centred elements agree since settings v7), offsetY the top.
    setPosition(CENTER_ANCHOR_X, CenterStack::underDashBoxTop());
    m_enabledNotices = NOTICE_DEFAULT;
    m_noticeDurationMs = DEFAULT_NOTICE_DURATION_MS;

    // The shared notice state starts over with the settings that step it
    NoticeTracker::getInstance().reset();

    setDataDirty();
}
