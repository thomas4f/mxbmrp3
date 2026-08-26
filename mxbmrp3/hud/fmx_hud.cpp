// ============================================================================
// hud/fmx_hud.cpp
// FMX (Freestyle Motocross) trick display HUD implementation
// ============================================================================
#include "fmx_hud.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/plugin_data.h"
#include "../core/fmx_manager.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

FmxHud::FmxHud() {
    DEBUG_INFO("FmxHud created");
    setDraggable(true);
    // Body card: this HUD draws a content BLOCK under its title, which is what the
    // themed card frames. Opt-in; see BaseHud::m_bContentCard.
    m_bContentCard = true;
    m_quads.reserve(ARC_SEGMENTS * 9 + COMBO_ARC_SEGMENTS * 2 + 10);  // 3 rotation arcs (bg+fill+markers each) + combo arc + backgrounds
    m_strings.reserve(15);

    m_trickStack.reserve(12);  // Max 10 display rows + margin
    setTextureBaseName("fmx_hud");
    resetToDefaults();
    rebuildRenderData();
}

bool FmxHud::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (trick state updates at telemetry rate)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void FmxHud::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Use standard dirty flag system - rebuilds only when telemetry
    // marks us dirty (~100Hz), skipping redundant frames at 480fps
    processDirtyFlags();
}

void FmxHud::rebuildLayout() {
    // For this HUD, full rebuild is still cheap
    rebuildRenderData();
}

SmallVec<float, 8> FmxHud::sectionHeights(const ScaledDimensions& dim) const {
    SmallVec<float, 8> out;
    const float head = sectionHeadingRowHeight(dim);
    // The caption band is the plan's; each block below carries its OWN
    // subheading row, and the air between two blocks is the seam the plan
    // spends ([content] margin, the sum of the facing margins) — which is what
    // the hand-rolled "separator gap" row used to approximate.
    if (isTrickStackEnabled()) {
        // Past trick rows use normal line height; last (active) row uses proportional advance
        const float activeTrickAdvance = dim.fontSizeLarge + (dim.lineHeightLarge - dim.fontSizeExtraLarge);
        float h = head + (m_maxChainDisplayRows - 1) * dim.lineHeightNormal + activeTrickAdvance;
        if (m_enabledRows & ROW_TRICK_STATS) {
            h += dim.lineHeightNormal;  // Trick stats row (duration + distance + rotation)
        }
        out.push_back(h);
    }
    if (m_enabledRows & ROW_COMBO_ARC) {
        const float comboArcHeight = dim.lineHeightNormal * 2.0f;
        const float comboOuterRadius = comboArcHeight * 0.9f;
        // The arc IS the block: the multiplier draws inside it and the three
        // score rows centre beside it. A trailing lineHeightSmall was stated
        // here (and advanced below) with nothing ever drawn in it — reported
        // as ~2 dead rows at the section's bottom once the ceil slack landed
        // on top of it.
        out.push_back(head + comboOuterRadius * 2.0f);
    }
    if (m_enabledRows & ROW_ARCS) {
        // labelHeight (Pitch/Yaw/Roll) + the arc art + its readout row.
        const float scaledArcDiameter = (ARC_RADIUS * 2.0f + ARC_THICKNESS) * m_fScale;
        out.push_back(head + dim.lineHeightNormal + scaledArcDiameter + dim.lineHeightSmall);
    }
    if (m_enabledRows & ROW_DEBUG_VALUES) {
        out.push_back(head + 3.0f * dim.lineHeightSmall);
    }
    // Every block switched off: one empty row, so the panel is still a panel.
    if (out.empty()) out.push_back(dim.lineHeightNormal);
    return out;
}

void FmxHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const FmxManager& fmx = FmxManager::getInstance();
    const Fmx::FmxScore& score = fmx.getScore();
    const Fmx::TrickInstance& trick = fmx.getActiveTrick();
    const Fmx::RotationTracker& rotation = fmx.getRotationTracker();

    // Layout constants
    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: standard HUD width (27 chars, same as IdealLapHud); the plan
    // owns the box, dynamic content height stays this HUD's own sum.
    int charWidth = 27;
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(charWidth, dim.fontSize);
    BaseHud::PanelWant want;
    want.contentW = contentWidth;
    want.sectionH = sectionHeights(dim);
    want.captionW = planTitleWidth(dim, "FMX", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& plan = planPanel(dim, want);
    float backgroundWidth = plan.width();
    addPlanBackground(plan, startX, startY);
    setBounds(startX, startY, startX + backgroundWidth, startY + plan.height());

    float contentStartX = plan.contentX();
    // Each block opens its own card by taking that section's content top; the
    // counter walks sectionHeights() in the same order it built them.
    size_t section = 0;
    float currentY = plan.contentY(0);

    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);

    // === Title === (the plan's caption row, above currentY)
    addPlanTitle(plan, "FMX", this->getFont(FontCategory::TITLE), textColor);

    // Chain tricks list — used by both trick stack and combo arc sections
    const auto& chainTricks = fmx.getChainTricks();

    // === Rows: Trick Stack (shows chain of tricks) — above the combo arc ===
    if (isTrickStackEnabled()) {
        currentY = plan.contentY(section++);
        addSectionHeading("Trick Stack", contentStartX, currentY, dim);
        currentY += sectionHeadingRowHeight(dim);

        const auto& endAnimStack = fmx.getChainEndAnimation();

        // Build list of tricks to display (oldest first, newest at bottom)
        m_trickStack.clear();

        // Helper: append a trick entry to the stack (formats name into fixed buffer)
        auto pushTrick = [this](Fmx::TrickType type, int multiplier, unsigned long color) {
            TrickStackEntry entry;
            Fmx::formatTrickName(type, multiplier, entry.name, sizeof(entry.name));
            entry.color = color;
            m_trickStack.push_back(entry);
        };

        // Check if chain-end animation is active (success or failure)
        if (endAnimStack.active) {
            // Linger the ended chain — green if completed, red if failed
            unsigned long endColor = endAnimStack.success
                ? this->getColor(ColorSlot::POSITIVE)
                : this->getColor(ColorSlot::NEGATIVE);

            for (size_t i = 0; i < endAnimStack.chainTricks.size(); ++i) {
                const auto& endedTrick = endAnimStack.chainTricks[i];
                pushTrick(endedTrick.type, endedTrick.multiplier, endColor);
            }
        } else {
            // Normal display logic
            bool pastThreshold = trick.progress >= Fmx::getMinProgress(trick.type);
            bool hasType = trick.type != Fmx::TrickType::NONE;
            bool hasCommittedActiveTrick =
                ((trick.state == Fmx::TrickState::ACTIVE && pastThreshold && hasType) ||
                 (trick.state == Fmx::TrickState::GRACE && hasType));

            unsigned long orangeColor = this->getColor(ColorSlot::WARNING);
            unsigned long yellowColor = this->getColor(ColorSlot::NEUTRAL);

            for (size_t i = 0; i < chainTricks.size(); ++i) {
                const auto& chainTrick = chainTricks[i];
                pushTrick(chainTrick.type, chainTrick.multiplier, yellowColor);
            }

            if (hasCommittedActiveTrick) {
                // Orange throughout ACTIVE+GRACE — only turns yellow once banked into chain
                pushTrick(trick.type, trick.multiplier, orangeColor);
            }
        }

        if (m_trickStack.empty()) {
            TrickStackEntry empty;
            empty.name[0] = '\0';
            empty.color = mutedColor;
            m_trickStack.push_back(empty);
        }

        // Calculate how many rows to display
        int maxRows = m_maxChainDisplayRows;
        int totalTricks = static_cast<int>(m_trickStack.size());
        int startIdx = 0;
        bool truncated = false;

        if (totalTricks > maxRows) {
            startIdx = totalTricks - maxRows;
            truncated = true;
        }

        int displayRows = std::min(totalTricks, maxRows);
        int emptyRows = maxRows - displayRows;

        // Skip empty rows at top (all use normal height — the large row is always the last displayed)
        currentY += emptyRows * dim.lineHeightNormal;

        // Display tricks: past tricks in medium title font, last trick (active) in large title font
        for (int i = startIdx; i < totalTricks; ++i) {
            const char* displayName = m_trickStack[i].name;
            char truncatedName[64];
            if (truncated && i == startIdx) {
                snprintf(truncatedName, sizeof(truncatedName), "... %s", displayName);
                displayName = truncatedName;
            }

            bool isLastRow = (i == totalTricks - 1);
            if (isLastRow) {
                // Active/current trick: large font with proportional gap matching Practice→Waiting
                // Practice: fontSizeExtraLarge(0.04) + lineHeightLarge(0.0444) → gap = 0.0044
                // Here: fontSizeLarge(0.03) + same gap → advance = 0.0344
                float activeTrickAdvance = dim.fontSizeLarge + (dim.lineHeightLarge - dim.fontSizeExtraLarge);
                addString(displayName, contentStartX, currentY, Justify::LEFT,
                    this->getFont(FontCategory::TITLE), m_trickStack[i].color, dim.fontSizeLarge);
                currentY += activeTrickAdvance;
            } else {
                // Past chain tricks: medium title font
                addString(displayName, contentStartX, currentY, Justify::LEFT,
                    this->getFont(FontCategory::TITLE), m_trickStack[i].color, dim.fontSize);
                currentY += dim.lineHeightNormal;
            }
        }
    }

    // === Row: Trick Stats (duration + distance + rotation) ===
    if (isTrickStackEnabled() && (m_enabledRows & ROW_TRICK_STATS)) {
        // Same visibility gate as trick name: must be past progress threshold
        bool pastThreshold = trick.progress >= Fmx::getMinProgress(trick.type);
        bool hasActiveTrick = trick.type != Fmx::TrickType::NONE &&
                              ((trick.state == Fmx::TrickState::ACTIVE && pastThreshold) ||
                               trick.state == Fmx::TrickState::GRACE);

        if (hasActiveTrick) {
            m_statsSnapshot.duration = trick.duration;
            m_statsSnapshot.distance = trick.distance;
            // Peak rotation on the trick's primary axis (pitch for flips, yaw for spins, etc.)
            switch (Fmx::getPrimaryAxis(trick.type)) {
                case Fmx::RotationAxis::PITCH: m_statsSnapshot.rotation = std::abs(rotation.peakPitch); break;
                case Fmx::RotationAxis::YAW:   m_statsSnapshot.rotation = std::abs(rotation.peakYaw);   break;
                case Fmx::RotationAxis::ROLL:  m_statsSnapshot.rotation = std::abs(rotation.peakRoll);  break;
                default:                        m_statsSnapshot.rotation = 0.0f;                         break;
            }
            m_statsSnapshot.hasData = true;
        } else if (trick.state == Fmx::TrickState::IDLE && score.chainCount == 0 &&
                   !fmx.getChainEndAnimation().active) {
            m_statsSnapshot = StatsSnapshot();
        }

        if (m_statsSnapshot.hasData) {
            char statsBuffer[64];
            if (m_statsSnapshot.rotation >= 1.0f) {
                snprintf(statsBuffer, sizeof(statsBuffer), "%.1fs  %.1fm  %.0fd",
                    m_statsSnapshot.duration, m_statsSnapshot.distance, m_statsSnapshot.rotation);
            } else {
                snprintf(statsBuffer, sizeof(statsBuffer), "%.1fs  %.1fm",
                    m_statsSnapshot.duration, m_statsSnapshot.distance);
            }
            addString(statsBuffer, contentStartX, currentY, Justify::LEFT,
                this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        }
        currentY += dim.lineHeightNormal;
    }

    // (No separator row: the air between two cards is the plan's seam.)
    if (m_enabledRows & ROW_COMBO_ARC) {
        currentY = plan.contentY(section++);
        addSectionHeading("Combo & Score", contentStartX, currentY, dim);
        currentY += sectionHeadingRowHeight(dim);
        // Match LeanWidget arc dimensions
        float comboArcHeight = dim.lineHeightNormal * 2.0f;
        float barWidthRef = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float arcThickness = barWidthRef * UI_ASPECT_RATIO;
        float outerRadius = comboArcHeight * 0.9f;
        float innerRadius = outerRadius - arcThickness;

        // Left-align arc: center is offset from contentStartX by the arc's horizontal radius
        float arcCenterX = contentStartX + outerRadius / UI_ASPECT_RATIO;
        float arcCenterY = currentY + outerRadius;

        // Background arc (full 360° ring) — dimmed but not tied to background opacity
        unsigned long arcBgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
        addArcSegment(arcCenterX, arcCenterY, innerRadius, outerRadius,
                      0.0f, 2.0f * PI, arcBgColor, COMBO_ARC_SEGMENTS);

        // Fill arc
        unsigned long comboFillColor = this->getColor(ColorSlot::NEUTRAL);

        const auto& endAnim = fmx.getChainEndAnimation();
        bool inChain = trick.state == Fmx::TrickState::CHAIN ||
                       (trick.state == Fmx::TrickState::ACTIVE && score.chainCount > 0);

        if (endAnim.active) {
            if (m_comboArcEndStartFill < 0.0f) {
                m_comboArcEndStartFill = m_comboArcFill;
            }
            m_comboArcGraceStartFill = -1.0f;

            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - endAnim.startTime).count();
            float animProgress = std::min(1.0f, elapsed / endAnim.duration);
            m_comboArcFill = m_comboArcEndStartFill * (1.0f - animProgress);

            comboFillColor = endAnim.success
                ? this->getColor(ColorSlot::POSITIVE)
                : this->getColor(ColorSlot::NEGATIVE);
        } else if (trick.state == Fmx::TrickState::GRACE) {
            m_comboArcEndStartFill = -1.0f;

            if (m_comboArcGraceStartFill < 0.0f) {
                m_comboArcGraceStartFill = m_comboArcFill;
            }
            auto now = std::chrono::steady_clock::now();
            float graceElapsed = std::chrono::duration<float>(now - trick.graceStartTime).count();
            float graceFraction = std::min(1.0f, graceElapsed / fmx.getConfig().landingGracePeriod);
            m_comboArcFill = m_comboArcGraceStartFill + (1.0f - m_comboArcGraceStartFill) * graceFraction;

            // Orange while filling — trick still at risk during grace
            comboFillColor = this->getColor(ColorSlot::WARNING);
        } else {
            m_comboArcGraceStartFill = -1.0f;
            m_comboArcEndStartFill = -1.0f;

            if (inChain) {
                float chainProgress = std::min(1.0f, score.chainElapsed / fmx.getConfig().chainPeriod);
                m_comboArcFill = 1.0f - chainProgress;
            } else {
                m_comboArcFill = 0.0f;
            }
        }

        if (m_comboArcFill > 0.01f) {
            float fillEndRad = m_comboArcFill * 2.0f * PI;
            int fillSegments = std::max(3, static_cast<int>(m_comboArcFill * COMBO_ARC_SEGMENTS));
            addArcSegment(arcCenterX, arcCenterY, innerRadius, outerRadius,
                          0.0f, fillEndRad, comboFillColor, fillSegments);
        }

        // Center text — chain multiplier (title font, always visible)
        const auto& endAnimArc = fmx.getChainEndAnimation();
        bool hasCommittedTrick =
            (trick.state == Fmx::TrickState::ACTIVE || trick.state == Fmx::TrickState::GRACE) &&
            trick.type != Fmx::TrickType::NONE &&
            trick.progress >= Fmx::getMinProgress(trick.type);

        // Calculate chain multiplier, including the active trick to show "potential"
        // multiplier — gives immediate feedback as the player starts a new trick.
        Fmx::TrickType extraType = hasCommittedTrick ? trick.type : Fmx::TrickType::NONE;
        float chainMultiplier = fmx.calculateChainMultiplier(chainTricks, extraType);
        char multiplierText[16];
        snprintf(multiplierText, sizeof(multiplierText), "%.1f", chainMultiplier);
        unsigned long multColor = textColor;
        // THE PAIR IS ONE BLOCK, centred as one. This centred the FIRST ROW on the
        // arc's middle (centre - fontSize/2, the single-row formula) and then hung
        // the "x" a small line below it, which put the visible pair half a small line
        // LOW inside the ring -- close enough to look like a rendering quirk rather
        // than arithmetic. The block runs from the value's top to the x's bottom, so
        // that span is what has to straddle the centre.
        const float multBlockH = dim.lineHeightSmall + dim.fontSize;
        float multValueY = arcCenterY - multBlockH * 0.5f;
        float multXY = multValueY + dim.lineHeightSmall;
        addString(multiplierText, arcCenterX, multValueY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), multColor, dim.fontSize);
        addString("x", arcCenterX, multXY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);

        // Score lines — to the right of the arc (title font, three rows)
        // Line 2 (chain score) aligns with the multiplier text inside the arc
        float arcRightEdge = arcCenterX + outerRadius / UI_ASPECT_RATIO;
        float scoreX = arcRightEdge + dim.paddingH * 0.5f;
        float lineSpacing = dim.fontSize * 1.3f;
        float multiplierY = arcCenterY - dim.fontSize * 0.5f;
        float scoreLine1Y = multiplierY - lineSpacing;
        float scoreLine2Y = multiplierY;
        float scoreLine3Y = multiplierY + lineSpacing;

        // Score labels and values — to the right of the arc
        float labelX = scoreX;
        float labelWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);  // "Total" (5) + space + gap = 7
        float valueX = labelX + labelWidth;

        // Line 1: Current trick score (always visible)
        {
            char trickScoreText[32];
            int displayTrickScore = 0;
            unsigned long trickScoreColor = textColor;

            if (endAnimArc.active && !endAnimArc.chainTricks.empty()) {
                // Linger the final trick's score — green on completion, red on failure
                displayTrickScore = endAnimArc.chainTricks.back().finalScore;
                trickScoreColor = endAnimArc.success
                    ? this->getColor(ColorSlot::POSITIVE)
                    : this->getColor(ColorSlot::NEGATIVE);
            } else if (hasCommittedTrick && trick.finalScore > 0) {
                displayTrickScore = trick.finalScore;
                // Orange throughout ACTIVE+GRACE — only safe once banked into chain
                trickScoreColor = this->getColor(ColorSlot::WARNING);
            }

            addString("Score", labelX, scoreLine1Y, Justify::LEFT,
                this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
            PluginUtils::formatScore(displayTrickScore, trickScoreText, sizeof(trickScoreText));
            addString(trickScoreText, valueX, scoreLine1Y, Justify::LEFT,
                this->getFont(FontCategory::TITLE), trickScoreColor, dim.fontSize);
        }

        // Line 2: Chain score (always visible, accumulates as tricks are banked)
        {
            char chainScoreText[32];
            int displayChainScore = score.chainScore;
            unsigned long chainScoreColor = textColor;

            if (endAnimArc.active) {
                displayChainScore = endAnimArc.chainScore;
                chainScoreColor = endAnimArc.success
                    ? this->getColor(ColorSlot::POSITIVE)
                    : this->getColor(ColorSlot::NEGATIVE);
            } else if (displayChainScore > 0) {
                chainScoreColor = this->getColor(ColorSlot::NEUTRAL);
            }

            addString("Chain", labelX, scoreLine2Y, Justify::LEFT,
                this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
            PluginUtils::formatScore(displayChainScore, chainScoreText, sizeof(chainScoreText));
            addString(chainScoreText, valueX, scoreLine2Y, Justify::LEFT,
                this->getFont(FontCategory::TITLE), chainScoreColor, dim.fontSize);
        }

        // Line 3: Session total (always visible)
        {
            char sessionScoreText[32];
            PluginUtils::formatScore(score.sessionScore, sessionScoreText, sizeof(sessionScoreText));
            addString("Total", labelX, scoreLine3Y, Justify::LEFT,
                this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
            addString(sessionScoreText, valueX, scoreLine3Y, Justify::LEFT,
                this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        }

        currentY += outerRadius * 2.0f;   // the arc is the block; see sectionHeights
    }

    // === Rows: Rotation Arcs (Pitch, Yaw, Roll) ===
    if (m_enabledRows & ROW_ARCS) {
        currentY = plan.contentY(section++);
        addSectionHeading("Rotation Arcs", contentStartX, currentY, dim);
        currentY += sectionHeadingRowHeight(dim);
        // Update arc display snapshot — same suppression pattern as trick name:
        // only switch to new data once the trick is classified, preventing
        // arcs from snapping to zero on brief bounces during a chain
        bool hasClassifiedTrick = trick.state == Fmx::TrickState::ACTIVE &&
                                  trick.type != Fmx::TrickType::NONE;
        bool freshUnclassified = trick.state == Fmx::TrickState::ACTIVE &&
                                 trick.type == Fmx::TrickType::NONE &&
                                 !m_arcSnapshot.hasData;
        // Airborne override: keep arcs live during a real flight even before
        // classification fires. With the 1s airborne debounce, a fresh
        // airborne trick stays type=NONE for up to a second; without this
        // override the arcs would freeze at the takeoff snapshot for the
        // whole flight. Chain transitions happen on the ground so this
        // doesn't reintroduce the brief-NONE-flicker the freeze guards against.
        bool airborneActive = trick.state == Fmx::TrickState::ACTIVE &&
                              trick.isCurrentlyAirborne;

        // Crash state: while the rider is crashed, freeze the arc snapshot and turn
        // the markers red, holding both for the entire crashed state (until recovery)
        // — matching the bars/g-force widgets. Without the !isCrashed guard on the
        // IDLE branch below, the failure animation's timer would expire mid-crash and
        // reset the arcs to live tracking while the rider is still down.
        const RiderTrackState* playerPos = PluginData::getInstance().getPlayerTrackPosition();
        bool isCrashed = playerPos && playerPos->crashed;

        if (hasClassifiedTrick || freshUnclassified || airborneActive) {
            // Live data from rotation tracker
            m_arcSnapshot.startPitch = rotation.startPitch;
            m_arcSnapshot.startYaw = rotation.startYaw;
            m_arcSnapshot.startRoll = rotation.startRoll;
            m_arcSnapshot.accumulatedPitch = rotation.accumulatedPitch;
            m_arcSnapshot.accumulatedYaw = rotation.accumulatedYaw;
            m_arcSnapshot.accumulatedRoll = rotation.accumulatedRoll;
            m_arcSnapshot.peakPitch = rotation.peakPitch;
            m_arcSnapshot.peakYaw = rotation.peakYaw;
            m_arcSnapshot.peakRoll = rotation.peakRoll;
            m_arcSnapshot.currentPitch = rotation.currentPitch;
            m_arcSnapshot.currentYaw = rotation.currentYaw;
            m_arcSnapshot.currentRoll = rotation.currentRoll;
            m_arcSnapshot.hasData = true;
        } else if (trick.state == Fmx::TrickState::IDLE && score.chainCount == 0 &&
                   !fmx.getChainEndAnimation().active && !isCrashed) {
            // Truly idle (and recovered) — show live start markers tracking current
            // bike orientation so they don't jump from 12 o'clock to takeoff angle on
            // launch. While still crashed we keep the snapshot frozen (see above).
            m_arcSnapshot = ArcSnapshot();
            m_arcSnapshot.startPitch = rotation.currentPitch;
            m_arcSnapshot.startYaw = rotation.currentYaw;
            m_arcSnapshot.startRoll = rotation.currentRoll;
        }
        // Else: GRACE, CHAIN, failure animation, or unclassified-with-prior-data → freeze snapshot

        // Arc diameter is ~0.076 at scale 1.0 (ARC_RADIUS*2 + ARC_THICKNESS)
        float scaledArcDiameter = (ARC_RADIUS * 2.0f + ARC_THICKNESS) * m_fScale;
        float labelHeight = dim.lineHeightNormal;
        float arcAreaHeight = labelHeight + scaledArcDiameter + dim.lineHeightSmall;
        float arcCenterY = currentY + labelHeight + scaledArcDiameter / 2.0f;

        // Scale arc radius with HUD scale
        float scaledRadius = ARC_RADIUS * m_fScale;
        float scaledThickness = ARC_THICKNESS * m_fScale;

        // Three arcs side by side
        float arcSpacing = contentWidth / 3.0f;
        float arc1X = contentStartX + arcSpacing * 0.5f;
        float arc2X = contentStartX + arcSpacing * 1.5f;
        float arc3X = contentStartX + arcSpacing * 2.5f;

        // Arc colors — standard pitch/yaw/roll coloring (red/green/blue)
        unsigned long arcBg = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
        // Markers freeze during grace/chain/bounce; on a crash they also turn red
        // (NEGATIVE) and hold until recovery (see the crash-freeze note above),
        // matching the bars/g-force widgets' crash convention.
        unsigned long arcMarker = isCrashed
            ? this->getColor(ColorSlot::NEGATIVE)
            : this->getColor(ColorSlot::PRIMARY);
        unsigned long pitchFill = ColorPalette::RED;
        unsigned long yawFill = ColorPalette::GREEN;
        unsigned long rollFill = ColorPalette::BLUE;

        // Render arcs from snapshot (frozen during grace/chain/bounce)
        addRotationArc(arc1X, arcCenterY, scaledRadius, scaledThickness,
            m_arcSnapshot.startPitch, m_arcSnapshot.accumulatedPitch, m_arcSnapshot.peakPitch,
            arcBg, pitchFill, arcMarker);

        addRotationArc(arc2X, arcCenterY, scaledRadius, scaledThickness,
            m_arcSnapshot.startYaw, m_arcSnapshot.accumulatedYaw, m_arcSnapshot.peakYaw,
            arcBg, yawFill, arcMarker);

        addRotationArc(arc3X, arcCenterY, scaledRadius, scaledThickness,
            m_arcSnapshot.startRoll, m_arcSnapshot.accumulatedRoll, m_arcSnapshot.peakRoll,
            arcBg, rollFill, arcMarker);

        // Labels above arcs — full axis names, colored to match fill.
        // addLabel: Small font size, row-centered (the label convention used
        // by the table headers in StandingsHud/FriendsHud/etc.)
        float labelY = currentY;
        addLabel("Pitch", arc1X, labelY, Justify::CENTER, this->getFont(FontCategory::STRONG), pitchFill, dim);
        addLabel("Yaw", arc2X, labelY, Justify::CENTER, this->getFont(FontCategory::STRONG), yawFill, dim);
        addLabel("Roll", arc3X, labelY, Justify::CENTER, this->getFont(FontCategory::STRONG), rollFill, dim);

        // Arc center text — 2 rows: start angle (muted), peak rotation (primary)
        // Both rows use fontSize (normal), so use lineHeightNormal for row advance.
        // Block = 2 rows of fontSize with one lineHeightNormal advance between baselines.
        char arcText[32];
        float blockHeight = dim.lineHeightNormal + dim.fontSize;  // baseline-to-baseline + descender of row 2
        float textY1 = arcCenterY - blockHeight * 0.5f;       // start angle
        float textY2 = textY1 + dim.lineHeightNormal;         // peak rotation

        const char* peakFmt = m_arcSnapshot.hasData ? "%+.0f" : "%.0f";

        // Pitch
        snprintf(arcText, sizeof(arcText), "%.0f", m_arcSnapshot.hasData ? m_arcSnapshot.startPitch : rotation.currentPitch);
        addString(arcText, arc1X, textY1, Justify::CENTER, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
        snprintf(arcText, sizeof(arcText), peakFmt, m_arcSnapshot.peakPitch);
        addString(arcText, arc1X, textY2, Justify::CENTER, this->getFont(FontCategory::DIGITS), textColor, dim.fontSize);

        // Yaw
        snprintf(arcText, sizeof(arcText), "%.0f", m_arcSnapshot.hasData ? m_arcSnapshot.startYaw : rotation.currentYaw);
        addString(arcText, arc2X, textY1, Justify::CENTER, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
        snprintf(arcText, sizeof(arcText), peakFmt, m_arcSnapshot.peakYaw);
        addString(arcText, arc2X, textY2, Justify::CENTER, this->getFont(FontCategory::DIGITS), textColor, dim.fontSize);

        // Roll
        snprintf(arcText, sizeof(arcText), "%.0f", m_arcSnapshot.hasData ? m_arcSnapshot.startRoll : rotation.currentRoll);
        addString(arcText, arc3X, textY1, Justify::CENTER, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
        snprintf(arcText, sizeof(arcText), peakFmt, m_arcSnapshot.peakRoll);
        addString(arcText, arc3X, textY2, Justify::CENTER, this->getFont(FontCategory::DIGITS), textColor, dim.fontSize);

        currentY += arcAreaHeight;
    }

    // === Rows: Debug Values (dev-only) — its own card too, so the block list
    // and the card list are the same list. ===
    if (m_enabledRows & ROW_DEBUG_VALUES) {
        currentY = plan.contentY(section++);
        addSectionHeading("Debug", contentStartX, currentY, dim);
        currentY += sectionHeadingRowHeight(dim);
        char valueBuffer[64];

        // Pitch
        snprintf(valueBuffer, sizeof(valueBuffer), "P: %+6.1f  v:%+6.1f  a:%+6.1f",
            rotation.currentPitch, rotation.pitchVelocity, rotation.accumulatedPitch);
        addString(valueBuffer, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::SMALL), mutedColor, dim.fontSizeSmall);
        currentY += dim.lineHeightSmall;

        // Yaw
        snprintf(valueBuffer, sizeof(valueBuffer), "Y: %+6.1f  v:%+6.1f  a:%+6.1f",
            rotation.currentYaw, rotation.yawVelocity, rotation.accumulatedYaw);
        addString(valueBuffer, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::SMALL), mutedColor, dim.fontSizeSmall);
        currentY += dim.lineHeightSmall;

        // Roll
        snprintf(valueBuffer, sizeof(valueBuffer), "R: %+6.1f  v:%+6.1f  a:%+6.1f",
            rotation.currentRoll, rotation.rollVelocity, rotation.accumulatedRoll);
        addString(valueBuffer, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::SMALL), mutedColor, dim.fontSizeSmall);
        currentY += dim.lineHeightSmall;
    }

}


void FmxHud::addRotationArc(float centerX, float centerY, float radius, float thickness,
                             float startAngle, float accumulatedAngle, float peakAngle,
                             unsigned long bgColor, unsigned long fillColor, unsigned long markerColor) {
    float innerRadius = radius - thickness / 2.0f;
    float outerRadius = radius + thickness / 2.0f;

    // Full background arc (360 degrees)
    float fullArcRad = 2.0f * PI;
    addArcSegment(centerX, centerY, innerRadius, outerRadius,
                  0.0f, fullArcRad, bgColor, ARC_SEGMENTS);

    // Convert angles to radians
    float startRad = startAngle * DEG_TO_RAD;

    // Fill arc — split into laps so the second rotation reads as visually
    // distinct instead of refilling the same color on top of itself. Lap 1
    // draws in the base fill color; lap 2 draws a darkened overlay only
    // where accumulated rotation has crossed past 360°, so the player can see
    // at a glance whether they're on their first or second turn. Darken
    // (rather than lighten) reads as "stacking" — the mental model is
    // layered rotation, not a bonus highlight.
    if (std::abs(accumulatedAngle) > 1.0f) {
        float absAccum = std::abs(accumulatedAngle);
        float dir = accumulatedAngle > 0 ? 1.0f : -1.0f;

        // Lap 1: base color, capped at 360°
        float lap1Deg = std::min(360.0f, absAccum);
        float lap1EndRad = startRad + dir * lap1Deg * DEG_TO_RAD;
        int lap1Segments = std::max(ARC_MIN_FILL_SEGMENTS,
            static_cast<int>(lap1Deg / 360.0f * ARC_SEGMENTS));
        addArcSegment(centerX, centerY, innerRadius, outerRadius,
                      std::min(startRad, lap1EndRad), std::max(startRad, lap1EndRad),
                      fillColor, lap1Segments);

        // Lap 2: lightened overlay for the portion past 360°, capped to keep
        // total fill within ARC_MAX_FILL_ROTATIONS.
        if (absAccum > 360.0f) {
            float lap2MaxDeg = (ARC_MAX_FILL_ROTATIONS - 1.0f) * 360.0f;
            float lap2Deg = std::min(lap2MaxDeg, absAccum - 360.0f);
            float lap2EndRad = startRad + dir * lap2Deg * DEG_TO_RAD;
            int lap2Segments = std::max(ARC_MIN_FILL_SEGMENTS,
                static_cast<int>(lap2Deg / 360.0f * ARC_SEGMENTS));
            unsigned long lap2Color = PluginUtils::darkenColor(fillColor, 0.6f);
            addArcSegment(centerX, centerY, innerRadius, outerRadius,
                          std::min(startRad, lap2EndRad), std::max(startRad, lap2EndRad),
                          lap2Color, lap2Segments);
        }
    }

    // Peak position marker (furthest extent reached — stays at max even if rotation reverses)
    // Clamp to same range as fill arc to prevent wrapping at high rotation counts
    float maxDeg = ARC_MAX_FILL_ROTATIONS * 360.0f;
    float clampedPeak = std::max(-maxDeg, std::min(maxDeg, peakAngle));
    float markerRad = (startAngle + clampedPeak) * DEG_TO_RAD;
    float markerInner = innerRadius - thickness * ARC_MARKER_OVERSHOOT;
    float markerOuter = outerRadius + thickness * ARC_MARKER_OVERSHOOT;
    addArcSegment(centerX, centerY, markerInner, markerOuter,
                  markerRad - ARC_PEAK_MARKER_HALF_WIDTH, markerRad + ARC_PEAK_MARKER_HALF_WIDTH,
                  markerColor, 2);

    // Start position marker (thinner tick showing takeoff angle)
    addArcSegment(centerX, centerY,
                  innerRadius - thickness * ARC_START_MARKER_OVERSHOOT,
                  outerRadius + thickness * ARC_START_MARKER_OVERSHOOT,
                  startRad - ARC_START_MARKER_HALF_WIDTH, startRad + ARC_START_MARKER_HALF_WIDTH,
                  markerColor, 1);
}

void FmxHud::resetToDefaults() {
    // Per-profile settings — position, visibility, scale, opacity, display elements
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.80f;
    m_fScale = 1.0f;
    setPosition(cellsX(133), cellsY(50));
    m_comboArcFill = 0.0f;
    m_comboArcGraceStartFill = -1.0f;
    m_comboArcEndStartFill = -1.0f;

    // Reset display state
    m_trickStack.clear();
    m_arcSnapshot = ArcSnapshot{};
    m_statsSnapshot = StatsSnapshot{};

    // Display settings (per-profile, like StandingsHud)
    m_enabledRows = ROW_DEFAULT;
    m_maxChainDisplayRows = 3;
    m_showDebugLogging = false;
    FmxManager::getInstance().setLoggingEnabled(false);

    setDataDirty();
}
