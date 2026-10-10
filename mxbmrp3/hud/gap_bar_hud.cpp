// ============================================================================
// hud/gap_bar_hud.cpp
// Gap Bar HUD - visualizes current lap progress vs best lap timing
// Shows a horizontal bar with current position, best lap marker, and live gap
// ============================================================================
#include "gap_bar_hud.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/widget_constants.h"
#include "center_stack.h"

using namespace PluginConstants;

GapBarHud::GapBarHud()
    : m_cachedDisplayRaceNum(-1)
    , m_cachedSessionGeneration(-1)
    , m_freezeDurationMs(FreezeDuration::FOLLOW_DEFAULT)
    , m_markerMode(MarkerMode::GHOST)
    , m_labelMode(LabelMode::NONE)
    , m_riderColorMode(RiderColorMode::RELATIVE_POS)
    , m_riderIconIndex(0)
    , m_showGapText(true)
    , m_showGapBar(true)
    , m_gapRangeMs(DEFAULT_RANGE_MS)
    , m_barWidthPercent(DEFAULT_WIDTH_PERCENT)
    , m_fMarkerScale(DEFAULT_MARKER_SCALE)
{
    for (int i = 0; i < SPLIT_SLOTS; ++i) {
        m_learnedSplitPos[i] = -1.0f;
        m_learnSplitCache[i] = -1;
    }
    // One-time setup
    // A themed body card behind the bar, like every other table HUD: without it the
    // bar sits straight on the frame while Standings and the rest give their content
    // a well to sit in.
    m_bContentCard = true;
    setDraggable(true);
    m_quads.reserve(4);    // Background, progress bar, best lap marker
    m_strings.reserve(1);  // Gap text

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("gap_bar_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool GapBarHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::IdealLap ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings ||
           dataType == DataChangeType::LapLog ||
           dataType == DataChangeType::TrackedRiders;
}

void GapBarHud::update() {
    // The live gap, the ghost's position and the rider's own are PluginData's
    // (PbGapTracker, driven from the central lap timer). This HUD tracks only what
    // it PRESENTS: the freeze that holds an official split/lap gap on screen for a
    // moment (official_gap_freeze.h). That detection runs while hidden too -- a
    // split crossed while the bar was off must not read as new the moment it is
    // shown again -- but it is a few integer compares; only the rebuild is gated
    // on visibility.
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Session change (new event/track/bike): restart the learned split positions.
    // (The freeze sees the change itself.)
    int currentGeneration = sessionData.sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        DEBUG_INFO_F("GapBarHud: Session reset detected (generation %d -> %d)",
            m_cachedSessionGeneration, currentGeneration);
        for (int i = 0; i < SPLIT_SLOTS; ++i) {
            m_learnedSplitPos[i] = -1.0f;
            m_learnSplitCache[i] = -1;
        }
        m_cachedSessionGeneration = currentGeneration;
        m_autoPeakMs = 0;
        if (isVisibleAnySurface()) setDataDirty();
    }

    // Spectate target change: adopt the new rider's splits so far, so they are
    // not taken for fresh crossings.
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (currentDisplayRaceNum != m_cachedDisplayRaceNum) {
        DEBUG_INFO_F("GapBarHud: Spectate target changed from %d to %d",
            m_cachedDisplayRaceNum, currentDisplayRaceNum);
        m_cachedDisplayRaceNum = currentDisplayRaceNum;
        m_autoPeakMs = 0;
        const CurrentLapData* currentLap = pluginData.getCurrentLapData();
        if (currentLap) {
            m_learnSplitCache[0] = currentLap->split1;
            m_learnSplitCache[1] = currentLap->split2;
            m_learnSplitCache[2] = currentLap->split3;
        }
        if (isVisibleAnySurface()) setDataDirty();
    }

    // The Auto range's peak belongs to one lap against one reference: start it
    // over when the gap comes back live (the next timed lap after the pits, or
    // after a session reset) and when the Reference changes. A committed lap
    // restarts it too (effectiveRangeMs); the out-lap from the pits is never
    // committed, which is why the live flip is needed as well.
    const Reference reference = getReference();
    const bool live = pluginData.hasValidLiveGap(reference);
    if ((live && !m_autoLive) || reference != m_autoReference) m_autoPeakMs = 0;
    m_autoLive = live;
    m_autoReference = reference;

    // Official splits and the line: freeze on their gap against the reference
    if (m_freeze.update(reference, HudDefaults::freezeMs(m_freezeDurationMs)) && isVisibleAnySurface()) {
        setDataDirty();
    }
    // Always, not only while shown: switching the ticks on mid-lap must not take
    // the split already behind the rider for one crossed right now.
    learnSplitPositions();

    // The live gap moves with the clock: refresh at ~60Hz while shown
    auto now = std::chrono::steady_clock::now();
    auto sinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastUpdate).count();
    if (sinceLastUpdate >= UPDATE_INTERVAL_MS) {
        m_lastUpdate = now;
        if (isVisibleAnySurface()) setDataDirty();
    }

    // OPTIMIZATION: Only process dirty flags and rebuild when visible
    if (isVisibleAnySurface()) {
        processDirtyFlags();
    } else {
        clearDataDirty();
        clearLayoutDirty();
    }
}

void GapBarHud::rebuildLayout() {
    // Layout changes require full rebuild
    rebuildRenderData();
}

void GapBarHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Get scaled dimensions
    auto dim = getScaledDimensions();

    // THE PANEL IS WHAT LINES UP, so the width setting scales the PANEL and the bar is
    // whatever interior the plan hands back. Notices, Timing and Version each pass
    // CenterStack::boxWidth as their minPanelW; passing the same number here at the 50%
    // default makes all four panels identical -- including whatever the plan rounds to,
    // which arithmetic on the bar could not guarantee. Sizing the BAR to their box
    // instead leaves this panel one padding wider per side, because the bar is content
    // and their box is a panel -- 2 * dim.paddingH, themed or not. The markers are drawn
    // from barWidth and re-derive, so a narrower bar moves them correctly; a panel that
    // does not line up with the stack it sits in is the worse of the two.
    float centerStackWidth = CenterStack::boxWidth(dim.fontSizeLarge, centerStackPaddingX());
    float basePanelWidth = 2.0f * centerStackWidth;
    float panelWidth = basePanelWidth * (static_cast<float>(m_barWidthPercent) / 100.0f);
    // One normal row, like the Version widget's content, so the bar lines up on the
    // shared grid with the Notices/Timing rows. Inner content (gap fill, markers, gap
    // text) is positioned relative to barHeight below, so it re-centers automatically.
    // Its own gap text stays cell-centred: a normal font in a normal row is exactly the
    // case addString's rowCenterOffset is built for.
    float barHeight = bigValueRowHeight(dim);

    // The bar's OWN inner insets -- how far the coloured fill sits inside the box.
    // Deliberately not the panel padding: these shape the bar graphic itself.
    float innerInsetH = dim.gridH(1) * layout().labelPaddingX;  // 0.5 char widths
    float innerInsetV = dim.gridV(BAR_PADDING_V_SCALE);           // quarter line height

    // startX is derived from the bar the PLAN returns, so it is computed after
    // planPanel below rather than here.
    float startX = 0.0f;
    // THE PANEL ANCHORS AT THE OFFSET, like every other HUD: the box top is 0 and the
    // bar sits one padding inside it. Anchoring the BAR at 0 and deriving the box as
    // (0 - paddingV) would make the panel's top edge a function of the padding, and
    // dim.paddingV IS contentPaddingY() -- so a theme that switches its body card off
    // ([card] hud-content = 0) would change the padding and slide this panel down while
    // Timing and Notices, which anchor their tops, stay put. It would also make the
    // DEFAULT position padding-dependent, so resetting under a theme and resetting
    // without one would write different numbers, and a theme switched on afterwards
    // would put the box top at y = 0 -- flush against the screen edge with its top
    // frame slice clipped. Pinned by tests/integration/tests/center_stack_theme_test.cpp.
    const float boxTop = 0.0f;
    float startY = 0.0f;   // set from the plan below, once the panel is placed

    // ==== BACKGROUND QUAD ====
    // addBackgroundQuad, not a hand-rolled quad: the themed nine-slice lives in the
    // helper, so a HUD that draws its own panel background gets no theme and renders
    // flat while every other panel is framed.
    // Panel padding wraps the bar, like every other HUD's box wraps its content, so
    // panelPaddingXCells/panelPaddingYCells apply here.
    // Optional caption. Default OFF, like the two panels below it in the stack --
    // this one is a bar, and a header over it is usually noise -- but available.
    // The box grows DOWNWARD (title above content, as everywhere else), which moves
    // the bar down; see resetToDefaults for what that costs the centre stack.
    // BOX-MODEL: the bar is the section's content; the card is its border box.
    // The panel wraps both through the plan, so the bar's interior, the card the
    // user sees and the panel height are one computation.
    PanelWant want;
    // The PANEL is the ask; the bar is read back from the plan below. contentW stays
    // 0 deliberately -- setting both would make the wider of the two win and leave
    // the panel two paddings too wide.
    wantCenterStackWidth(want, panelWidth);
    want.sectionH = { barHeight };
    // LARGE, like every other full HUD. The tier is opt-in and defaults to
    // Normal, which is right for a gauge and wrong for a panel with a table in
    // it -- left unsaid, this panel wears the widget caption size next to
    // siblings that do not.
    want.captionW = planTitleWidth(dim, "Gap Bar", TitleTier::Large);
    want.tier = TitleTier::Large;
    // The bar IS this panel: unthemed, its coloured fill takes the cell of panel
    // padding rather than sitting inside it. Same outer rect either way; with a
    // theme on, the frame keeps its ring. See PanelWant::contentFillsPanel.
    want.contentFillsPanel = true;
    PanelPlan& plan = planPanel(dim, want);
    // Draw at what the plan RETURNED, not what was asked: the last section's
    // box absorbs the panel's ceil remainder (panel_box.h), and a bar sized
    // to the ask stops a strip above the card it is meant to fill.
    barHeight = plan.sectionBoxH();
    // The bar IS the plan's content column, so it inherits the panel's rounding and
    // the four panels share an edge exactly.
    float barWidth = plan.contentW();

    const float insetL = static_cast<float>(plan.g.rowsX) * plan.cellW;
    const float boxWidth = plan.width();
    const float boxHeight = plan.height();

    // CENTRE-ANCHORED, as all four centred elements are: offsetX is this panel's
    // CENTRE, so the layout is just half its own width to the left of it and a width
    // change grows the bar symmetrically instead of walking an edge.
    //
    // A LAYOUT MUST NOT READ m_fOffsetX: snapping against the LIVE offset here would
    // make m_fBoundsLeft a function of the offset -- and the drag path snaps
    // `m_fBoundsLeft + newOffsetX` itself (base_hud.cpp). Two snaps, each reading the
    // other's output a frame late, jitter the bar left and right under the cursor for
    // as long as it is held. Every other HUD's bounds are a pure function of its
    // layout, and that is what makes the one snap in the drag path the only one there
    // is.
    const float boxLeft = centerAnchoredPanelLeft(boxWidth);
    startX = boxLeft + insetL;
    addPlanBackground(plan, boxLeft, boxTop);
    addPlanTitle(plan, "Gap Bar",
                 this->getColor(ColorSlot::PRIMARY));
    // THE CARD'S DRAWN BOX, not the content band inside it: the bar IS the card here,
    // so its fill, its markers and its gap text all belong to the box the player sees.
    // The band is the same thing while [content] border is symmetric and sits high in
    // the card when it is not (PanelPlan::sectionBoxY) -- which would put the gap
    // text above centre and leave the fill short of the content it is drawn behind.
    startY = plan.sectionBoxY();

    // Common inner dimensions
    float innerWidth = barWidth - innerInsetH * 2.0f;
    float innerHeight = barHeight - innerInsetV * 2.0f;

    // THE SLAB THE GAP TEXT LANDS ON, or 0 for none. The text is CENTER-justified on
    // exactly the point the fill grows from (both take plan.sectionBoxCenterX()), so
    // whenever a fill is drawn, half the digits sit on it -- and the text's own colour
    // is a NEGATIVE/POSITIVE slot too, which is red-on-red the moment the two agree --
    // unreadable at high background opacity. The Notices slabs beside this one make
    // the same correction (captionOnSlabColor).
    //
    // Recorded rather than re-derived, because the fill and the text do NOT always
    // read the same number: the fill is always the LIVE gap (see the header just
    // below), while the text may be showing a FROZEN one, so their signs can differ
    // and the ink is legible over the opposite colour. What the correction needs is
    // the slab actually under the glyphs.
    unsigned long gapTextSlab = 0;

    // ==== GAP BAR (grows from center based on live gap - never frozen) ====
    if (m_showGapBar) {
        // Always use live gap for the bar visualization (use cached value)
        int gap = 0;
        const PluginData& data = PluginData::getInstance();

        if (data.hasValidLiveGap(getReference())) {
            gap = data.getLiveGap(getReference());
        }

        // Calculate bar extent: gap / range = percentage of half-bar
        // Positive gap (behind) = grow left (red), negative gap (ahead) = grow right (green)
        float gapRatio = static_cast<float>(gap) / static_cast<float>(effectiveRangeMs(gap));
        gapRatio = std::max(-1.0f, std::min(1.0f, gapRatio));  // Clamp to -1..1

        // THE FILL'S TRAVEL SPANS THE CARD'S DRAWN BOX, the horizontal half of the
        // same change the outset below makes vertically -- stated in the card's own
        // terms (PanelPlan::sectionBoxW), not as barWidth plus the LEFT inset mirrored,
        // which reaches the card's edges only while the [content] terms are
        // left/right symmetric -- and not the INNER rect, inset by innerInsetH, which
        // exists for the rider markers and would stop a maxed-out gap short of the
        // box.
        //
        // THE CONSEQUENCE, stated because it is a real trade: the fill and the rider
        // markers do not share one scale. The markers stay on the inner rect, so a
        // full-scale fill reaches slightly past where a marker can sit. They measure
        // different things -- the fill is your gap, the markers are riders -- and the
        // alternative is a fill that cannot touch the box it lives in.
        float halfWidth = plan.sectionBoxW() / 2.0f;
        float centerX = plan.sectionBoxCenterX();

        if (std::abs(gapRatio) > 0.001f) {
            float quadX, quadWidth;
            unsigned long fillColor;

            if (gapRatio > 0.0f) {
                // Behind (slower) - grow left from center, red
                quadWidth = halfWidth * gapRatio;
                quadX = centerX - quadWidth;
                gapTextSlab = this->getColor(ColorSlot::NEGATIVE);
                fillColor = PluginUtils::applyOpacity(gapTextSlab, m_fBackgroundOpacity);
            } else {
                // Ahead (faster) - grow right from center, green
                quadWidth = halfWidth * (-gapRatio);
                quadX = centerX;
                gapTextSlab = this->getColor(ColorSlot::POSITIVE);
                fillColor = PluginUtils::applyOpacity(gapTextSlab, m_fBackgroundOpacity);
            }

            // FULL HEIGHT OF THE CONTENT BOX, not the marker inset. The fill is the one
            // thing here whose SIZE is the reading -- how far off the reference you are
            // -- and drawn a quarter row short at each end it floats inside the card
            // with a dark margin above and below. innerInsetV exists to keep the rider
            // MARKERS clear of the box edges (icons, not a bar), not the fill.
            //
            // Only the vertical is freed. The fill's WIDTH stays on the inner rect,
            // because that is the coordinate system the markers are placed in: a fill
            // scaled to the box while markers are scaled to the inner rect would put a
            // full-scale fill edge somewhere no marker can ever reach.
            // THROUGH THE THEME'S BUTTON SLICES, like the Notices slab beside it: both
            // are a coloured block whose colour is the reading, and a theme that gives
            // its buttons a shape should give these the same one. A hand-rolled
            // SOLID_COLOR quad is the duplication addButtonQuad exists to end -- and
            // draws a flat rectangle inside a bevelled card on a themed gap bar.
            //
            // NOT opaque: like the notice slabs, this is a translucent reading over the
            // track rather than a control, and the button rule (a thing you click stays
            // legible) would flatten it to a solid box.
            // ...and it spans the card exactly, because startY/barHeight ARE the card.
            // Adding an outset back on at BOTH ends (the top inset twice) would be
            // short at the bottom by however much the two insets differ -- invisible
            // on a symmetric border, short of its own content on an asymmetric one.
            addButtonQuad(quadX, startY, quadWidth, barHeight, fillColor, /*opaque=*/false);
        }
    }

    // ==== SPLIT TICKS (over the fill, under the riders) ====
    if (m_showSplits) {
        renderSplitTicks(startY, barHeight, startX + innerInsetH, innerWidth);
    }

    // ==== RIDER MARKERS (icons instead of vertical bars) ====
    // Renders self, ghost, and/or opponents based on marker mode; OFF draws none.
    if (m_markerMode != MarkerMode::OFF) {
        renderRiderMarkers(startX + innerInsetH, startY + innerInsetV, innerWidth, innerHeight, dim);
    }

    // ==== GAP TEXT (centered inside bar, primary color) - conditionally rendered ====
    if (!m_showGapText) {
        // Skip gap text - user wants pure flat map mode
        // The BOX, not the bar: the box is what the user sees and grabs, and with a
    // title on, bar-only bounds would leave the caption undraggable.
    setBounds(boxLeft, boxTop, boxLeft + boxWidth, boxTop + boxHeight);
        return;
    }

    // ==== GAP TEXT (centered inside bar, primary color) ====
    // X: the CARD's centre (PanelPlan::sectionBoxCenterX -- the bar's own centre
    // only while the [content] terms are symmetric), Y: INK-centred in the bar,
    // the same solve TimingHud's big time and the Notices message use.
    float gapTextX = plan.sectionBoxCenterX();
    // INK-centred, not cell-centred: a digit's ink sits 0.11 of its cell above the
    // cell's middle, so text centred by its glyph CELL floats that much high inside a
    // bar it is drawn on top of (~3px at 1080p).
    // inkCenteredY over barHeight, not bigValueTextY: that helper centres in a
    // bigValueRowHeight box, and barHeight is the CARD's interior when there is a card.
    // Passing the row would leave the text off the bar's centre whenever the two
    // differ.
    //
    // THE LARGE SIZE, matching the Timing panel's big time below it: the two are the
    // same kind of reading in the same stack. It costs no height -- the row is
    // bigValueRowHeight (one NORMAL row) whatever the glyph is, and inkCenteredY
    // solves the placement for the size it is handed, which is the case that helper
    // exists for and the one Timing uses.
    float gapTextY = inkCenteredY(startY, barHeight, dim.fontSizeLarge);

    char gapBuffer[32];
    unsigned long gapColor;

    // The frozen official gap from a split/lap crossing, else the live one (full precision)
    int gap = 0;
    if (m_freeze.shownGap(getReference(), &gap)) {
        PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), gap);
        gapColor = this->deltaColor(gap);
    } else {
        // No gap to show
        strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
        // MUTED, like the Timing panel's big time with no lap to show. A placeholder
        // in the primary colour reads as a value -- the one thing it is not -- and at
        // the large size it draws at, a bright "-" is the loudest thing in the stack
        // while saying the least.
        gapColor = this->getColor(ColorSlot::MUTED);
    }

    // Lift the ink off the slab if the two are too close in luma -- see gapTextSlab
    // above for why the slab is carried down here rather than re-derived from the sign
    // of the number being printed. A no-op when no fill was drawn, and a no-op when
    // the ink already clears the slab (legibleOnFill keeps the hue whenever it can).
    if (gapTextSlab != 0) {
        gapColor = inkOnSlabColor(gapColor, gapTextSlab, m_fBackgroundOpacity);
    }

    // Gap text (monospace font, large size to match Timing, centered)
    addString(gapBuffer, gapTextX, gapTextY, Justify::CENTER,
              this->getFont(FontCategory::DIGITS), gapColor, dim.fontSizeLarge);

    // Set bounds for drag detection
    // The BOX, not the bar: the box is what the user sees and grabs, and with a
    // title on, bar-only bounds would leave the caption undraggable.
    setBounds(boxLeft, boxTop, boxLeft + boxWidth, boxTop + boxHeight);
}

// No setScale override: this panel is CENTRE-ANCHORED (offsetX is its centre),
// so the layout recentres on every width change and scaling needs no offset
// compensation. A setScaleKeepingCenter override ON TOP of that recentring
// double-compensates: each scale step walks the stored centre sideways by half
// the width change. Pinned by center_stack_theme_test's scale case.

void GapBarHud::setBarWidth(int percent) {
    // Clamp to valid range
    percent = std::max(MIN_WIDTH_PERCENT, std::min(percent, MAX_WIDTH_PERCENT));
    if (percent == m_barWidthPercent) return;

    // Apply the new width - no position adjustment needed since offset is bar center
    m_barWidthPercent = percent;
    setDataDirty();
}

void GapBarHud::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_reference = Reference::SESSION_PB;
    m_referenceDefault = true;
    // Off by DEFAULT, not unavailable -- the toggle is in the Gap Bar tab. Switching
    // it on grows the box DOWNWARD, so the bar and everything under it move down by
    // the band's height; center_stack.h derives the two panels below from box heights
    // it computes WITHOUT a title, so expect to nudge them.
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    setScale(1.0f);
    // First box of the center-top stack; see hud/center_stack.h for the whole
    // specification. One cell down from the screen edge, aligning with the
    // settings/camera buttons' row.
    // The BOX TOP, plainly, because that is what the offset means here (see
    // rebuildRenderData); adding the live paddingV back would make the default
    // depend on the theme in force when it is written.
    setPosition(CENTER_ANCHOR_X, CenterStack::stackBoxTop());

    // Settings
    m_freezeDurationMs = FreezeDuration::FOLLOW_DEFAULT;
    m_markerMode = MarkerMode::GHOST;  // Default to ghost-only
    m_labelMode = LabelMode::NONE;     // No labels by default (like MapHud default)
    m_labelAnchor = LabelAnchor::BELOW;  // ...and under the marker, like the other two
    m_riderColorMode = RiderColorMode::RELATIVE_POS;  // Default to position-based coloring
    m_riderIconIndex = 0;              // 0 = use default icon (circle-chevron-up)
    m_showGapText = true;              // Show gap text by default
    m_showGapBar = true;               // Show gap visualization bars by default
    m_gapRangeMs = DEFAULT_RANGE_MS;
    m_autoPeakMs = 0;
    m_barWidthPercent = DEFAULT_WIDTH_PERCENT;
    m_fMarkerScale = DEFAULT_MARKER_SCALE;
    m_showSplits = true;

    m_freeze.reset();
    setDataDirty();
}

// The fill's range: the setting, or under Auto the round step that holds the
// lap's largest gap so far. A new lap (a lap committed) starts the peak over.
int GapBarHud::effectiveRangeMs(int gapMs) {
    if (m_gapRangeMs != RANGE_AUTO) return m_gapRangeMs;
    const unsigned stamp = PluginData::getInstance().getPbGapTracker().lastLapStamp();
    if (stamp != m_autoLapStamp) {
        m_autoLapStamp = stamp;
        m_autoPeakMs = 0;
    }
    m_autoPeakMs = std::max(m_autoPeakMs, std::abs(gapMs));
    return std::max(MIN_RANGE_MS, PluginUtils::niceGapScaleMs(m_autoPeakMs));
}

// ============================================================================
// Rider position update for flat map mode
// ============================================================================
void GapBarHud::updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions) {
    if (numVehicles <= 0 || positions == nullptr) {
        m_riderPositions.clear();
        return;
    }

    // Only store if we're showing opponents
    if (m_markerMode == MarkerMode::OPPONENTS || m_markerMode == MarkerMode::GHOST_OPPONENTS) {
        m_riderPositions.assign(positions, positions + numVehicles);
        if (isVisibleAnySurface()) {
            setDataDirty();
        }
    }
}

// ============================================================================
// Calculate rider color based on color mode setting (like MapHud/RadarHud)
// ============================================================================
unsigned long GapBarHud::calculateRiderColor(int riderRaceNum, int displayRaceNum) const {
    const PluginData& pluginData = PluginData::getInstance();

    // Get lap data for position-based modulation (only in race sessions)
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    const StandingsData* riderStanding = pluginData.getStanding(riderRaceNum);
    bool isRace = pluginData.isRaceSession();
    int playerLaps = (isRace && playerStanding) ? playerStanding->numLaps : 0;
    int riderLaps = (isRace && riderStanding) ? riderStanding->numLaps : 0;
    int lapDiff = riderLaps - playerLaps;

    // Check if this is a tracked rider (has custom color - overrides color mode)
    const RaceEntryData* entry = pluginData.getRaceEntry(riderRaceNum);
    if (entry) {
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
        const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);
        if (trackedConfig) {
            // Tracked rider - use their configured color with lap-based modulation
            unsigned long baseColor = trackedConfig->color;

            // Apply position-based color modulation (like RadarHud)
            // lapDiff is already zeroed in non-race sessions above
            if (lapDiff >= 1) {
                // Rider is ahead by laps - lighten color
                baseColor = PluginUtils::lightenColor(baseColor, 0.4f);
            } else if (lapDiff <= -1) {
                // Rider is behind by laps - darken color
                baseColor = PluginUtils::darkenColor(baseColor, 0.6f);
            }

            return baseColor;
        }
    }

    // Apply color based on selected mode
    switch (m_riderColorMode) {
        case RiderColorMode::RELATIVE_POS: {
            // Position-based coloring - only meaningful in race sessions
            if (!pluginData.isRaceSession()) {
                return this->getColor(ColorSlot::NEUTRAL);
            }
            int playerPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
            int riderPosition = pluginData.getDisplayPositionForRaceNum(riderRaceNum);

            return PluginUtils::getRelativePositionColor(
                playerPosition, riderPosition, playerLaps, riderLaps,
                this->getColor(ColorSlot::NEUTRAL),
                this->getColor(ColorSlot::WARNING),
                this->getColor(ColorSlot::TERTIARY));
        }

        case RiderColorMode::BRAND: {
            // Bike brand color
            if (entry) {
                return PluginUtils::applyOpacity(entry->bikeBrandColor, 0.75f);
            }
            return this->getColor(ColorSlot::PRIMARY);  // Fallback if no entry
        }

        case RiderColorMode::UNIFORM:
        default:
            // Uniform: riders use the primary color (matching Map and Radar, and their
            // name color in the standings); accent is reserved for the player.
            return this->getColor(ColorSlot::PRIMARY);
    }
}

// ============================================================================
// Render a marker icon (rotated 90° right for directional icons only)
// ============================================================================
void GapBarHud::renderMarkerIcon(float centerX, float centerY, float size,
                                  int spriteIndex, unsigned long color, int shapeIndex) {
    // Define corner offsets (square icon)
    float halfSize = size / 2.0f;

    // Only rotate directional icons (like chevrons) - non-directional icons (like circles) stay upright
    bool shouldRotate = TrackedRidersManager::shouldRotate(shapeIndex);

    // Rotation: 90° clockwise to point right (direction of travel)
    // cos(90°) = 0, sin(90°) = 1
    float cosAngle = shouldRotate ? 0.0f : 1.0f;
    float sinAngle = shouldRotate ? 1.0f : 0.0f;

    float corners[4][2] = {
        {-halfSize, -halfSize},  // Top-left
        {-halfSize,  halfSize},  // Bottom-left
        { halfSize,  halfSize},  // Bottom-right
        { halfSize, -halfSize}   // Top-right
    };

    SPluginQuad_t sprite;
    for (int i = 0; i < 4; i++) {
        float dx = corners[i][0];
        float dy = corners[i][1];

        // Rotate in uniform space
        float rotX = dx * cosAngle - dy * sinAngle;
        float rotY = dx * sinAngle + dy * cosAngle;

        // Apply aspect ratio to X and position
        sprite.m_aafPos[i][0] = centerX + rotX / UI_ASPECT_RATIO;
        sprite.m_aafPos[i][1] = centerY + rotY;
        applyOffset(sprite.m_aafPos[i][0], sprite.m_aafPos[i][1]);
    }

    sprite.m_iSprite = spriteIndex;
    sprite.m_ulColor = color;
    m_quads.push_back(sprite);
}

// ============================================================================
// Render all rider markers (self, ghost, opponents based on mode)
// ============================================================================
void GapBarHud::renderRiderMarkers(float innerX, float innerY, float innerWidth, float innerHeight,
                                    const ScaledDimensions& dim) {
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Get icon sprite index and shape index for rotation check
    const AssetManager& assetMgr = AssetManager::getInstance();
    int spriteIndex;
    int globalShapeIndex;
    if (m_riderIconIndex > 0) {
        // User selected a specific icon
        spriteIndex = assetMgr.iconSpriteForShape(m_riderIconIndex);
        globalShapeIndex = m_riderIconIndex;
    } else {
        // Default to circle-chevron-up
        spriteIndex = assetMgr.getIconSpriteIndex("circle-chevron-up");
        globalShapeIndex = assetMgr.shapeIndexForSprite(spriteIndex);
    }

    // Icon size scaled with HUD and marker scale (matches MapHud/StandingsHud pattern)
    // DEFAULT_MARKER_BASE_SIZE is full size, so halfSize = 0.006 * scale * markerScale
    float iconSize = DEFAULT_MARKER_BASE_SIZE * m_fScale * m_fMarkerScale;
    float iconHalfSize = iconSize / 2.0f;

    // Y centre of the bar -- of the ICON when there is no label, and of the icon and
    // its label TOGETHER when there is. This bar is one text row tall, so a label
    // centred the naive way hangs half out of the panel; marker_label.h owns the
    // shift, and it is per-marker because the local player's icon and label are both
    // boosted and so want a bigger one.
    const float boxCenterY = innerY + innerHeight / 2.0f;
    auto centerFor = [&](float halfSize, float boost) {
        if (m_labelMode == LabelMode::NONE) return boxCenterY;
        return boxCenterY + MarkerLabel::blockCenterShift(
            m_labelAnchor, halfSize, dim.fontSizeSmall * m_fMarkerScale * boost);
    };
    const float markerY = centerFor(iconHalfSize, 1.0f);

    // === Render opponent markers (if enabled) - render FIRST so they're behind ===
    if (m_markerMode == MarkerMode::OPPONENTS || m_markerMode == MarkerMode::GHOST_OPPONENTS) {
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();

        for (const auto& pos : m_riderPositions) {
            if (pos.raceNum == displayRaceNum) continue;  // Skip self

            float trackPos = pos.trackPos;
            if (trackPos < 0.0f || trackPos > 1.0f) continue;

            // Calculate color using RELATIVE_POS logic (handles tracked rider colors)
            unsigned long riderColor = calculateRiderColor(pos.raceNum, displayRaceNum);

            // Check for tracked rider custom icon
            int riderSpriteIndex = spriteIndex;  // Default to global icon
            int riderShapeIndex = globalShapeIndex;
            const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
            if (entry) {
                const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);
                if (trackedConfig) {
                    riderSpriteIndex = assetMgr.iconSpriteForShape(trackedConfig->shapeIndex);
                    riderShapeIndex = trackedConfig->shapeIndex;
                }
            }

            // Calculate X position on bar
            float markerX = innerX + (innerWidth * trackPos);

            // Render icon (only rotates if directional icon like chevron)
            renderMarkerIcon(markerX, markerY, iconSize, riderSpriteIndex, riderColor, riderShapeIndex);

            // Render label if enabled
            if (m_labelMode != LabelMode::NONE) {
                int position = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
                renderMarkerLabel(markerX, markerY, iconHalfSize, pos.raceNum, position, dim);
            }
        }
    }

    // === Render ghost (best lap) marker ===
    if ((m_markerMode == MarkerMode::GHOST || m_markerMode == MarkerMode::GHOST_OPPONENTS) &&
        PluginData::getInstance().hasValidLiveGap(getReference())) {
        float bestLapProgress = PluginData::getInstance().getPbGhostProgress(getReference());
        if (bestLapProgress >= 0.0f && bestLapProgress <= 1.0f) {
            float markerX = innerX + (innerWidth * bestLapProgress);

            // Ghost uses darkened color - check if player is tracked
            unsigned long ghostColor;
            int ghostSpriteIndex = spriteIndex;
            int ghostShapeIndex = globalShapeIndex;

            // Ghost is a dimmed version of the live self marker: darkened tracked color
            // when tracked, otherwise a darkened accent (matching the self marker default).
            ghostColor = PluginUtils::darkenColor(this->getColor(ColorSlot::ACCENT), 0.5f);

            const RaceEntryData* selfEntry = pluginData.getRaceEntry(displayRaceNum);
            if (selfEntry) {
                const TrackedRiderConfig* selfTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(selfEntry->name);
                if (selfTrackedConfig) {
                    ghostColor = PluginUtils::darkenColor(selfTrackedConfig->color, 0.5f);
                    ghostSpriteIndex = assetMgr.iconSpriteForShape(selfTrackedConfig->shapeIndex);
                    ghostShapeIndex = selfTrackedConfig->shapeIndex;
                }
            }

            renderMarkerIcon(markerX, markerY, iconSize, ghostSpriteIndex, ghostColor, ghostShapeIndex);
            // No label for ghost - it's your own best lap
        }
    }

    // === Render self marker (always on top) ===
    const float selfTrackPos = PluginData::getInstance().getDisplayRiderTrackPos();
    if (selfTrackPos > 0.001f) {
        float markerX = innerX + (innerWidth * selfTrackPos);
        // A touch larger than the pack, like MapHud's local-player marker -- on a bar
        // whose whole subject is YOUR gap, the one marker that is you should be the
        // one you find first. See MarkerLabel::PLAYER_BOOST.
        const float selfBoost = MarkerLabel::boost(true);
        const float selfIconSize = iconSize * selfBoost;

        // Check if player is tracked - use their configured color and shape (like RadarHud).
        // Default to the accent slot so the player's own marker matches StandingsHud/MapHud.
        unsigned long selfColor = this->getColor(ColorSlot::ACCENT);
        int selfSpriteIndex = spriteIndex;
        int selfShapeIndex = globalShapeIndex;

        const RaceEntryData* selfEntry = pluginData.getRaceEntry(displayRaceNum);
        if (selfEntry) {
            const TrackedRiderConfig* selfTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(selfEntry->name);
            if (selfTrackedConfig) {
                selfColor = selfTrackedConfig->color;
                selfSpriteIndex = assetMgr.iconSpriteForShape(selfTrackedConfig->shapeIndex);
                selfShapeIndex = selfTrackedConfig->shapeIndex;
            }
        }

        const float selfY = centerFor(selfIconSize / 2.0f, selfBoost);
        renderMarkerIcon(markerX, selfY, selfIconSize, selfSpriteIndex, selfColor, selfShapeIndex);

        // Render label for self if enabled
        if (m_labelMode != LabelMode::NONE) {
            int position = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
            renderMarkerLabel(markerX, selfY, selfIconSize / 2.0f, displayRaceNum, position,
                              dim, selfBoost);
        }
    }
}

// ============================================================================
// Render a label below a marker (position and/or race number) - matches MapHud style
// ============================================================================
void GapBarHud::renderMarkerLabel(float centerX, float centerY, float iconHalfSize,
                                   int raceNum, int position, const ScaledDimensions& dim,
                                   float playerBoost) {
    if (m_labelMode == LabelMode::NONE) return;

    // Scale font size by marker scale, and by the local player's boost so the self
    // marker's label grows with the marker (MapHud does the same; see marker_label.h).
    const float labelFontSize = dim.fontSizeSmall * m_fMarkerScale * playerBoost;

    // Where the label sits relative to the icon: shared with MapHud and RadarHud.
    const MarkerLabel::Placement lp = MarkerLabel::place(
        m_labelAnchor, centerX, centerY, iconHalfSize, labelFontSize);

    // Build label string based on mode (matching MapHud format)
    char labelStr[20];
    if (!MarkerLabel::format(m_labelMode, position, raceNum, labelStr, sizeof(labelStr))) {
        return;  // Nothing to render (e.g. POSITION mode with no valid position)
    }

    // Podium colors for position labels (P1/P2/P3) like MapHud
    unsigned long labelColor =
        MarkerLabel::color(m_labelMode, position, this->getColor(ColorSlot::PRIMARY));

    // THE STANDARD DROP SHADOW, exactly as MapHud draws the same label: one string
    // with skipShadow=false, and HudManager::collectRenderData lays the shadow in
    // behind it from [Display] dropShadowOffsetX/Y, honouring the global toggle and
    // any per-HUD dropShadow override.
    //
    // Not a hand-rolled black outline (the same string four times at +-5% of the
    // font): that is four quads instead of one, a shadow the [Display] offsets
    // cannot move, and an outline that stays on with drop shadows switched OFF.
    // (Rider ICONS are sprite quads with their own baked outlines and take no shadow
    // either way -- this is only the text.)
    addString(labelStr, lp.x, lp.y, lp.justify,
              this->getFont(FontCategory::SMALL), labelColor, labelFontSize, false);
}
