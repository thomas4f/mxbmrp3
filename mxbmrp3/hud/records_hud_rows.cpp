// ============================================================================
// hud/records_hud_rows.cpp
// The Records HUD's table rows: the PB and status lines before a fetch, the
// fetched records around the player's PB (or, scrolled, a plain window of
// them), and one row's cells. Split from records_hud.cpp, which keeps the
// panel, the selectors and the fetch orchestration.
// ============================================================================
#include "records_hud.h"
#include "records_window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"

using namespace PluginConstants;

// The record rows: the player's PB and/or a status line before a fetch (or when
// it found nothing), else the paginated server records with the PB inserted.
void RecordsHud::buildRecordRows(RowCursor& cur, const std::vector<RecordEntry>& allRecords,
                                 const std::string& lastError, float contentStartX) {
    const ScaledDimensions& dim = cur.dim;
    float& currentY = cur.y;
    int& rowsRendered = cur.rowsRendered;

    // Get player's all-time PB for this track+bike (or category, depending on scope)
    const SessionData& session = PluginData::getInstance().getSessionData();
    const StatsPersonalBestData* playerPB = nullptr;
    std::string pbBikeName;  // Which bike set the PB (may differ from current in Category mode)
    int playerPosition = -1;  // -1 = no PB, 0+ = position in records

    if (session.trackId[0] != '\0' && session.bikeName[0] != '\0') {
        playerPB = StatsManager::getInstance().getPersonalBest(session.trackId, session.bikeName, &pbBikeName);
        if (playerPB && playerPB->isValid()) {
            playerPosition = findPlayerPositionInRecords(allRecords, playerPB->lapTime);
        }
    }
    const char* playerPBBike = pbBikeName.empty() ? session.bikeName : pbBikeName.c_str();

    // One record row (see addRecordRow); advances the cursor.
    auto renderRecordRow = [&](auto... args) { addRecordRow(cur, args...); };


    FetchState currentState = m_fetchState.load();
    bool hasFetched = (currentState == FetchState::SUCCESS || !allRecords.empty());

    // Format player's PB date from timestamp (used in all branches)
    const char* playerName = session.riderName[0] != '\0' ? session.riderName : TR("You");
    bool hasPlayerPB = (playerPB && playerPB->isValid());
    char playerDateStr[16] = "";
    if (hasPlayerPB && playerPB->timestamp > 0) {
        std::tm tm;
        if (localtime_s(&tm, &playerPB->timestamp) == 0) {
            snprintf(playerDateStr, sizeof(playerDateStr), "%04d-%02d-%02d",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        }
    }

    if (!hasFetched) {
        // Before fetch or error: the player's own PB first, whatever the state
        if (hasPlayerPB) {
            renderRecordRow(1, playerName, playerPBBike, playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
        }
        if (currentState == FetchState::FETCH_ERROR) {
            // A failed fetch reads NEGATIVE, as the Compare button does, in a row.
            char errorMessage[64];
            if (!lastError.empty()) {
                snprintf(errorMessage, sizeof(errorMessage), TR("Compare failed: %s"), lastError.c_str());
            } else {
                strncpy_s(errorMessage, sizeof(errorMessage), TR("Compare failed - try again"), sizeof(errorMessage) - 1);
            }
            addString(errorMessage, contentStartX, currentY,
                      Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::NEGATIVE), dim.fontSize);
            currentY += dim.lineHeightNormal;
            rowsRendered++;
        } else if (currentState != FetchState::FETCHING) {
            // NOT FETCHED YET (FETCHING has the button's "..."): the empty-state notice
            // (addBlockedNotice), MUTED as nothing is wrong, centred in the rows the
            // records fill (>= 2 left: floor 3, PB 1). Takes no rows: the panel holds.
            const int freeRows = m_recordsToShow - rowsRendered;
            addBlockedNotice(cur.plan.sectionBoxCenterX(),
                             currentY + static_cast<float>(freeRows) * dim.lineHeightNormal * 0.5f,
                             dim.lineHeightNormal, dim.fontSize,
                             TR("Click Compare to load records"), ColorSlot::MUTED,
                             m_bAutoFetch ? nullptr : TR("Or turn on Auto-fetch in MXBMRP3 Settings > Records"));
        }
    } else if (allRecords.empty()) {
        // Fetched but no records found - show player's PB and/or message
        if (hasPlayerPB) {
            renderRecordRow(1, playerName, playerPBBike, playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
        }
        // Show "no records" message - counts as a row to maintain layout
        addString(TR("No records found for this track/category"), contentStartX, currentY,
                  Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
        currentY += dim.lineHeightNormal;
        rowsRendered++;
    } else {
        const PlayerPbRow player{hasPlayerPB, playerName, playerPBBike, playerPB, playerDateStr, playerPosition};
        addPaginatedRecords(cur, allRecords, player);
    }
}

// Has records - show with StandingsHud-style pagination.
void RecordsHud::addPaginatedRecords(RowCursor& cur, const std::vector<RecordEntry>& allRecords,
                                     const PlayerPbRow& player) {
    const bool hasPlayerPB = player.has;
    const char* playerName = player.name;
    const char* playerPBBike = player.bike;
    const StatsPersonalBestData* playerPB = player.pb;
    const char* playerDateStr = player.date;
    const int playerPosition = player.position;
    const int totalRecords = static_cast<int>(allRecords.size());
    auto renderRecordRow = [&](auto... args) { addRecordRow(cur, args...); };

    static constexpr int TOP_POSITIONS = 3;

    // Moved by the scrollbar: a plain window of the whole table, the player's PB
    // in its place (index playerIdx). At home, the composed view below; home is
    // the window that view's rows sit closest to.
    m_scrollListed = true;
    const int playerIdx = hasPlayerPB ? std::min(playerPosition, totalRecords) : -1;
    const int listTotal = totalRecords + (hasPlayerPB ? 1 : 0);
    m_scroll.setHome(playerIdx >= TOP_POSITIONS ? playerIdx - m_recordsToShow / 2 : 0);
    const int first = m_scroll.layout(listTotal, m_recordsToShow, 0);
    if (!m_scroll.atHome()) {
        // A row more while the scrollback glides; HudScroll::clipRows cuts them.
        const float rowsTop = cur.y;
        const size_t rowsQuad = m_quads.size(), rowsString = m_strings.size();
        for (int k = first; k < first + m_scroll.drawRows() && k < listTotal; ++k) {
            if (k == playerIdx) {
                renderRecordRow(0, playerName, playerPBBike, playerPB->lapTime, playerPB->sector1,
                                playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
                continue;
            }
            const auto& record = allRecords[static_cast<size_t>(playerIdx >= 0 && k > playerIdx ? k - 1 : k)];
            renderRecordRow(record.position, record.rider, record.bike, record.laptime, record.sector1,
                            record.sector2, record.sector3, record.sector4, record.date, false);
        }
        m_scroll.clipRows(*this, rowsQuad, m_quads.size(), rowsString, m_strings.size(), rowsTop,
                          cur.dim.lineHeightNormal);
        // The table ends where it does at rest; the footer follows.
        const int extra = m_scroll.extraRows();
        cur.y -= static_cast<float>(extra) * cur.dim.lineHeightNormal;
        cur.rowsRendered -= extra;
        return;
    }

    // Strategy (like StandingsHud):
    // - If player is in top 3 (or no PB): show first N records with player inserted
    // - If player is beyond top 3: show top 3, then context around player position

    // Helper lambda to render a range of server records, optionally inserting player
    auto renderRecordRange = [&](int startIdx, int endIdx, bool insertPlayer) {
        for (int i = startIdx; i <= endIdx && i < totalRecords; i++) {
            // Insert player row before this record if player position matches
            if (insertPlayer && hasPlayerPB && playerPosition == i) {
                renderRecordRow(0, playerName, playerPBBike, playerPB->lapTime,
                                playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
            }
            // Render the server record
            const auto& record = allRecords[i];
            renderRecordRow(record.position, record.rider, record.bike, record.laptime,
                            record.sector1, record.sector2, record.sector3, record.sector4, record.date, false);
        }
        // Insert player at end if they're after the last record in range
        if (insertPlayer && hasPlayerPB && playerPosition > endIdx && playerPosition <= endIdx + 1) {
            renderRecordRow(0, playerName, playerPBBike, playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
        }
    };

    if (!hasPlayerPB || playerPosition < TOP_POSITIONS) {
        // Player is in top 3 (or no PB) - show first N records with player inserted
        int recordsToShow = std::min(totalRecords, m_recordsToShow - (hasPlayerPB ? 1 : 0));
        renderRecordRange(0, recordsToShow - 1, true);
    } else {
        // Player is beyond top 3 - show top 3, then context around player
        // 1. Show top 3 records
        int topToShow = std::min(totalRecords, TOP_POSITIONS);
        renderRecordRange(0, topToShow - 1, false);

        // 2. Calculate context window around player — pure arithmetic,
        //    unit-tested in tests/unit/test_records_window.cpp.
        const RecordsWindow::Range context =
            RecordsWindow::computeContext(totalRecords, playerPosition,
                                          m_recordsToShow, TOP_POSITIONS);
        const int contextStart = context.start;
        const int contextEnd = context.end;

        // 3. Render context around player
        for (int i = contextStart; i <= contextEnd && i < totalRecords; i++) {
            // Insert player row at correct position
            if (hasPlayerPB && playerPosition == i) {
                renderRecordRow(0, playerName, playerPBBike, playerPB->lapTime,
                                playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
            }
            const auto& record = allRecords[i];
            renderRecordRow(record.position, record.rider, record.bike, record.laptime,
                            record.sector1, record.sector2, record.sector3, record.sector4, record.date, false);
        }
        // Insert player at end if they're after the last context record
        if (hasPlayerPB && playerPosition > contextEnd) {
            renderRecordRow(0, playerName, playerPBBike, playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
        }
    }
}

// A single record row.
// isPlayerRow: add highlight background, skip position column
// sector1/2/3/4: -1 if not available (CBR or player PB)
void RecordsHud::addRecordRow(RowCursor& cur, int position, const char* rider, const char* bike, int laptime,
                              int sector1, int sector2, int sector3, int sector4, const char* date, bool isPlayerRow) {
    const PanelPlan& plan = cur.plan;
    const ScaledDimensions& dim = cur.dim;
    float& currentY = cur.y;
    int& rowsRendered = cur.rowsRendered;

    // Add highlight background quad for player row
    if (isPlayerRow) {
        // THE CONTENT COLUMN, like StandingsHud's row highlight -- one owner
        // (plan.rowBandX/W) for every row band in the plugin. Spanning the raw
        // backgroundWidth would run it out over the frame's edge slice; spanning
        // the card's INTERIOR runs it out over the card's own padding.
        addRowHighlight(plan.rowBandX(), currentY,
                        plan.rowBandW(), dim.lineHeightNormal,
                        PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT),
                                                  ROW_SELECT_ALPHA));
    }

    // Position ("1", "2", ... under a "P" header, as StandingsHud) - skip for player row
    if (isColumnEnabled(COL_POS) && !isPlayerRow) {
        char posStr[8];
        snprintf(posStr, sizeof(posStr), "%d", position);
        unsigned long posColor;
        if (position == 1) posColor = PodiumColors::GOLD;
        else if (position == 2) posColor = PodiumColors::SILVER;
        else if (position == 3) posColor = PodiumColors::BRONZE;
        else posColor = this->getColor(ColorSlot::TERTIARY);  // P4+ as in StandingsHud
        addString(posStr, m_columns.pos, currentY, Justify::LEFT, this->getFont(FontCategory::DIGITS), posColor, dim.fontSize);
    }

    // Rider (truncate if too long; shared ellipsis truncation)
    if (isColumnEnabled(COL_RIDER)) {
        std::string riderStr = PluginUtils::fitText(rider, COL_RIDER_WIDTH - 1);
        // Player row keeps same column alignment (skip position but stay in rider column)
        addString(riderStr.c_str(), m_columns.rider, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    }

    // Bike (truncate if too long; shared ellipsis truncation)
    if (isColumnEnabled(COL_BIKE)) {
        std::string bikeStr = PluginUtils::fitText(bike, COL_BIKE_WIDTH - 1);
        addString(bikeStr.c_str(), m_columns.bike, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  this->getColor(ColorSlot::SECONDARY), dim.fontSize);
    }

    // Sector times (S1, S2, S3, and S4 for 4-sector games - always toggled together)
    if (isColumnEnabled(COL_SECTORS)) {
        char sectorStr[12];

        // S1
        if (sector1 > 0) {
            PluginUtils::formatLapTime(sector1, sectorStr, sizeof(sectorStr));
            addString(sectorStr, m_columns.sector1, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        } else {
            addString(Placeholders::GENERIC, m_columns.sector1, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
        }

        // S2
        if (sector2 > 0) {
            PluginUtils::formatLapTime(sector2, sectorStr, sizeof(sectorStr));
            addString(sectorStr, m_columns.sector2, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        } else {
            addString(Placeholders::GENERIC, m_columns.sector2, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
        }

        // S3
        if (sector3 > 0) {
            PluginUtils::formatLapTime(sector3, sectorStr, sizeof(sectorStr));
            addString(sectorStr, m_columns.sector3, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        } else {
            addString(Placeholders::GENERIC, m_columns.sector3, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
        }

#if GAME_SECTOR_COUNT >= 4
        // S4 (4-sector games only)
        if (sector4 > 0) {
            PluginUtils::formatLapTime(sector4, sectorStr, sizeof(sectorStr));
            addString(sectorStr, m_columns.sector4, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        } else {
            addString(Placeholders::GENERIC, m_columns.sector4, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
        }
#else
        (void)sector4;  // Suppress unused warning for 3-sector games
#endif
    }

    // Laptime
    if (isColumnEnabled(COL_LAPTIME)) {
        char laptimeStr[16];
        if (laptime > 0) {
            PluginUtils::formatLapTime(laptime, laptimeStr, sizeof(laptimeStr));
            addString(laptimeStr, m_columns.laptime, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        } else {
            addString(Placeholders::GENERIC, m_columns.laptime, currentY, Justify::LEFT,
                      this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
        }
    }

    // Date
    if (isColumnEnabled(COL_DATE)) {
        const bool hasDate = date && date[0] != '\0';
        addString(hasDate ? date : Placeholders::GENERIC, m_columns.date, currentY,
                  Justify::LEFT, this->getFont(FontCategory::DIGITS),
                  this->getColor(hasDate ? ColorSlot::SECONDARY : ColorSlot::MUTED), dim.fontSize);
    }

    currentY += dim.lineHeightNormal;
    rowsRendered++;
}
