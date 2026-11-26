// ============================================================================
// hud/records_hud.cpp
// Displays lap records fetched from external data providers via HTTP
// ============================================================================
#include "records_hud.h"
#include "default_slots.h"
#include "../core/layout_config.h"
#include "records_window.h"
#include "timing_hud.h"

#include "../game/game_config.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_manager.h"
#include "../core/hud_manager.h"
#include "../core/settings_manager.h"
#include "../core/input_manager.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"
#include "../core/ui_config.h"

// The HTTP transport + JSON parse live in core/records_fetcher.{h,cpp}
// (included via records_hud.h); this file keeps presentation + fetch
// orchestration only.
#include <windows.h>

using namespace PluginConstants;

// ============================================================================
// Column Positions
// ============================================================================

RecordsHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns) {
    float scaledFontSize = layoutDefaults().fontSizeNormal * scale;
    float x = contentStartX;

    // Position each column based on what's enabled before it
    // Order: POS, RIDER, BIKE, SECTORS (S1/S2/S3/S4), LAPTIME, DATE
    pos = x;
    if (enabledColumns & COL_POS) x += PluginUtils::calculateMonospaceTextWidth(COL_POS_WIDTH, scaledFontSize);

    rider = x;
    if (enabledColumns & COL_RIDER) x += PluginUtils::calculateMonospaceTextWidth(COL_RIDER_WIDTH, scaledFontSize);

    bike = x;
    if (enabledColumns & COL_BIKE) x += PluginUtils::calculateMonospaceTextWidth(COL_BIKE_WIDTH, scaledFontSize);

    // Sector columns - always toggled together
    // 3-sector games (MX Bikes): S1, S2, S3
    // 4-sector games (GP Bikes): S1, S2, S3, S4
    sector1 = x;
    sector2 = x + PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH, scaledFontSize);
    sector3 = x + PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH * 2, scaledFontSize);
#if GAME_SECTOR_COUNT >= 4
    sector4 = x + PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH * 3, scaledFontSize);
    if (enabledColumns & COL_SECTORS) x += PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH * 4, scaledFontSize);
#else
    sector4 = 0;  // Not used for 3-sector games
    if (enabledColumns & COL_SECTORS) x += PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH * 3, scaledFontSize);
#endif

    laptime = x;
    if (enabledColumns & COL_LAPTIME) x += PluginUtils::calculateMonospaceTextWidth(COL_LAPTIME_WIDTH, scaledFontSize);

    date = x;
    // DATE is last column, no need to advance x
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

RecordsHud::RecordsHud()
    : m_columns()
    , m_provider(DataProvider::CBR)
    , m_categoryIndex(0)
    , m_fetchState(FetchState::IDLE)
    , m_recordsProvider(DataProvider::CBR)
    , m_fetchButtonHovered(false)
    , m_fetchResultTimestamp(0)
    , m_fetchStartTimestamp(0)
    , m_wasOnCooldown(false)
{
    // One-time setup
    setDraggable(true);
    // Body card: this HUD draws a content BLOCK under its title, which is what the
    // themed card frames. Opt-in; see BaseHud::m_bContentCard.
    m_bContentCard = true;
    m_quads.reserve(2);  // Background + fetch button
    m_strings.reserve(60);  // Header rows + 10 record rows * 5 cols + footer
    m_records.reserve(MAX_RECORDS);
    m_clickRegions.reserve(5);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("records_hud");

    // Set all configurable defaults
    resetToDefaults();

    // Initialize column positions (after resetToDefaults sets m_enabledColumns)
    m_columns = ColumnPositions(START_X + layoutDefaults().panelPaddingX, m_fScale, m_enabledColumns);

    // Build initial category list (after resetToDefaults sets m_provider)
    buildCategoryList();

    rebuildRenderData();
}

RecordsHud::~RecordsHud() {
    // Wait for any ongoing fetch to complete
    joinFetchThread();
}

void RecordsHud::joinFetchThread() {
    // Called by HudManager::clear() BEFORE the cached HUD pointers are
    // nulled: the fetch thread calls getTimingHud() on completion, so it
    // must be gone before m_pTiming is reset (and before TimingHud is
    // destroyed). Also called from the destructor as a safety net.
    // Delegates to the fetcher, which owns the worker thread.
    m_fetcher.join();
}

// ============================================================================
// Category Management
// ============================================================================

void RecordsHud::buildCategoryList() {
    m_categoryList.clear();
    m_categoryList.push_back("All");   // index 0: no filter; shown translated
    m_categoryList.push_back("MX-E OEM");
    m_categoryList.push_back("MX1 OEM");
    m_categoryList.push_back("MX2 OEM");
    m_categoryList.push_back("MX1-2T OEM");
    m_categoryList.push_back("MX2-2T OEM");
    m_categoryList.push_back("MX3 OEM");
}

int RecordsHud::findCategoryIndex(const char* category) const {
    // Skip "All" (index 0)
    for (size_t i = 1; i < m_categoryList.size(); ++i) {
        if (m_categoryList[i] == category) {
            return static_cast<int>(i);
        }
    }
    return -1;  // Not found
}

const char* RecordsHud::getCurrentCategoryDisplay() const {
    if (m_categoryIndex >= 0 && m_categoryIndex < static_cast<int>(m_categoryList.size())) {
        return m_categoryIndex == 0 ? TR("All") : m_categoryList[m_categoryIndex].c_str();
    }
    return TR("All");
}

// ============================================================================
// HTTP Fetch Operations (transport + parse in core/records_fetcher.cpp)
// ============================================================================

void RecordsHud::startFetch() {
    // Cooldown check - prevent spam (silently ignore if on cooldown)
    unsigned long now = GetTickCount();
    if (now - m_fetchStartTimestamp < FETCH_COOLDOWN_MS) {
        return;
    }

    // Don't start if already fetching
    FetchState expected = FetchState::IDLE;
    if (!m_fetchState.compare_exchange_strong(expected, FetchState::FETCHING)) {
        // Also allow re-fetch from SUCCESS/ERROR states
        expected = FetchState::SUCCESS;
        if (!m_fetchState.compare_exchange_strong(expected, FetchState::FETCHING)) {
            expected = FetchState::FETCH_ERROR;
            if (!m_fetchState.compare_exchange_strong(expected, FetchState::FETCHING)) {
                return;  // Already fetching
            }
        }
    }

    // Record fetch start time for cooldown
    m_fetchStartTimestamp = now;

    // Snapshot every input the fetcher's buildRequestUrl() needs, here on the game
    // thread, before the worker starts. This is the only place these are read for
    // the fetch, so the worker never touches PluginData or game-thread-mutated
    // state directly.
    DataProvider fetchProvider = m_provider;
    const SessionData& session = PluginData::getInstance().getSessionData();
    std::string fetchTrackName = session.trackName;
    std::string fetchCategory;
    if (m_categoryIndex > 0 && m_categoryIndex < static_cast<int>(m_categoryList.size())) {
        fetchCategory = m_categoryList[m_categoryIndex];
    }
    // else: index 0 ("All") => no category filter (empty)

    // Start new fetch thread (the fetcher joins any previous, finished worker
    // first). The completion callback runs ON THE WORKER THREAD.
    m_fetcher.start(fetchProvider, std::move(fetchTrackName), std::move(fetchCategory),
                    [this](RecordsFetcher::Result&& result) {
                        onFetchComplete(std::move(result));
                    });
}

// Runs ON THE FETCH WORKER THREAD (see RecordsFetcher). Only touches
// m_recordsMutex-guarded members, atomics, and setDataDirty(); the cross-HUD
// TimingHud touch below is why HudManager::clear() joins the fetch thread
// BEFORE nulling its cached HUD pointers (joinFetchThread).
void RecordsHud::onFetchComplete(RecordsFetcher::Result&& result) {
    if (result.parsed) {
        storeParsedRecords(std::move(result));
        m_fetchState = FetchState::SUCCESS;
        DEBUG_INFO("RecordsHud: Fetch successful");
        // Notify TimingHud so it can update RC reference time immediately
        HudManager::getInstance().getTimingHud().setDataDirty();
    } else {
        // Atomic state first (noexcept). The string assignment can itself
        // throw bad_alloc — if the failure was bad_alloc, allocating a new
        // error string defeats the fetcher's exception barrier. Acquire the
        // lock once outside the inner try so a second lock_guard in a nested
        // catch can't throw std::system_error and propagate out of the thread.
        m_fetchState = FetchState::FETCH_ERROR;
        {
            MutexLock lock(m_recordsMutex);
            try {
                m_lastError = std::move(result.error);
            } catch (...) {
                m_lastError.clear();  // noexcept
            }
        }
    }
    m_fetchResultTimestamp = GetTickCount();
    setDataDirty();
}

void RecordsHud::storeParsedRecords(RecordsFetcher::Result&& result) {
    // Lock for all member variable writes. The result was parsed with the
    // provider snapshotted on the game thread in startFetch(), never the live
    // m_provider - the user can cycle providers mid-fetch and the response must
    // be parsed with the schema it was requested with.
    MutexLock lock(m_recordsMutex);
    m_records = std::move(result.records);
    m_recordsProvider = result.provider;  // Track which provider these records came from
    if (!result.apiNotice.empty()) {
        m_apiNotice = std::move(result.apiNotice);
    }
}


// ============================================================================
// Click Handling
// ============================================================================

void RecordsHud::handleClick(float mouseX, float mouseY) {
    // An open list takes the click: an entry picks its category, anything else
    // only closes it (the click under the list was not aimed at what it covers).
    if (m_categoryOpen) {
        closeCategoryList();
        for (const auto& region : m_clickRegions) {
            if (region.type == ClickRegionType::CATEGORY_OPTION &&
                isPointInRect(mouseX, mouseY,
                    region.x + m_fOffsetX, region.y + m_fOffsetY, region.width, region.height)) {
                m_categoryIndex = region.entry;
                SettingsManager::getInstance().markDirty();
                break;
            }
        }
        return;
    }
    // Click regions store base positions, add offset for hit testing
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY,
                region.x + m_fOffsetX, region.y + m_fOffsetY, region.width, region.height)) {
            bool saveSettings = false;
            switch (region.type) {
                case ClickRegionType::PROVIDER_LEFT:
                    cycleProvider(-1);
                    saveSettings = true;
                    break;
                case ClickRegionType::PROVIDER_RIGHT:
                    cycleProvider(1);
                    saveSettings = true;
                    break;
                case ClickRegionType::CATEGORY_LEFT:
                    cycleCategory(-1);
                    saveSettings = true;
                    break;
                case ClickRegionType::CATEGORY_RIGHT:
                    cycleCategory(1);
                    saveSettings = true;
                    break;
                case ClickRegionType::CATEGORY_BOX:
                    m_categoryOpen = true;
                    break;
                case ClickRegionType::CATEGORY_OPTION:   // only drawn while open, handled above
                    break;
                case ClickRegionType::FETCH_BUTTON:
                    startFetch();
                    break;
            }
            setDataDirty();
            if (saveSettings) {
                // Deferred: persisted on leave-track / Save button, never mid-ride.
                SettingsManager::getInstance().markDirty();
            }
            break;
        }
    }
}

void RecordsHud::cycleProvider(int direction) {
    int count = static_cast<int>(DataProvider::COUNT);
    int current = static_cast<int>(m_provider);
    current = (current + direction + count) % count;
    m_provider = static_cast<DataProvider>(current);
}

void RecordsHud::cycleCategory(int direction) {
    int count = static_cast<int>(m_categoryList.size());
    if (count == 0) return;

    // Cycle through all categories including "All" (index 0)
    m_categoryIndex = (m_categoryIndex + direction + count) % count;
}

void RecordsHud::closeCategoryList() {
    m_categoryOpen = false;
    m_hoveredCategory = -1;
    setDataDirty();
}

int RecordsHud::findPlayerPositionInRecords(const std::vector<RecordEntry>& records, int playerPBTime) const {
    if (playerPBTime <= 0) return -1;  // No valid PB
    if (records.empty()) return 0;     // Faster than all (no records to compare)

    // Find where player's PB would rank
    for (size_t i = 0; i < records.size(); i++) {
        if (playerPBTime < records[i].laptime) {
            return static_cast<int>(i);  // Player is faster, would be at this position
        }
    }

    // Player is slower than all records
    return static_cast<int>(records.size());
}

// ============================================================================
// Update and Rendering
// ============================================================================

bool RecordsHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::SessionData);  // Rebuild when track/category changes
}

void RecordsHud::update() {
    const SessionData& session = PluginData::getInstance().getSessionData();
    bool shouldAutoFetch = false;

    // Clear records when event ends (track becomes empty)
    // Lock before reading m_records - the fetch thread may be reallocating it
    if (session.trackName[0] == '\0') {
        MutexLock lock(m_recordsMutex);
        if (!m_records.empty() || m_lastSessionTrackName[0] != '\0') {
            m_records.clear();
            m_fetchState = FetchState::IDLE;
            m_lastSessionTrackName[0] = '\0';  // Reset tracked track
            m_lastSessionCategory[0] = '\0';  // Reset tracked category
            setDataDirty();
        }
    } else {
        // Track changed (entered new event) - auto-fetch if enabled
        // Bound the copy by the SOURCE buffer, not the destination: trackName is
        // char[NAME_BUFFER_SIZE] (100) while the destination is char[256], so a
        // destination-derived count (255) would let strncpy_s read up to 155 bytes
        // past the end of session.trackName if it ever arrived unterminated. It
        // never does today (safeCopy() in game_adapter_base.h always terminates),
        // but that invariant lives in another file and nothing here enforces it.
        if (strcmp(session.trackName, m_lastSessionTrackName) != 0) {
            strncpy_s(m_lastSessionTrackName, sizeof(m_lastSessionTrackName), session.trackName, sizeof(session.trackName) - 1);
            shouldAutoFetch = true;
        }
    }

    // Auto-update category selection when session category changes (event start)
    // Note: Game doesn't allow changing category mid-event, so this only triggers on join
    if (strcmp(session.category, m_lastSessionCategory) != 0) {
        strncpy_s(m_lastSessionCategory, sizeof(m_lastSessionCategory), session.category, sizeof(m_lastSessionCategory) - 1);
        if (session.category[0] != '\0') {
            int idx = findCategoryIndex(session.category);
            if (idx >= 0) {
                m_categoryIndex = idx;
                setDataDirty();
            }
        }
    }

    // Perform auto-fetch if enabled and conditions met
    // Note: This runs even when hidden so TimingHud can access records
    if (m_bAutoFetch && shouldAutoFetch && m_fetchState != FetchState::FETCHING) {
        startFetch();
    }

    // Skip UI processing when not visible
    if (!isVisibleAnySurface()) {
        m_categoryOpen = false;   // hidden, the list shuts: it does not reopen with the HUD
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // The scrollbar first (not under an open category list): a press on it is
    // not the table's.
    if (!m_categoryOpen && m_scroll.poll(*this)) setDataDirty();

    // Handle mouse input for click regions — only when the cursor is usable.
    // Gating on isValid mirrors StandingsHud/MapHud and means a hidden cursor
    // (e.g. Menu-Only Cursor) can't hover or click the fetch button.
    const auto& inputManager = InputManager::getInstance();
    // Shift the cursor into this HUD's build space so the fetch button lines up when
    // the HUD is dragged to a different spot on the companion (no-op in-game).
    CursorPosition cursor = inputManager.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    bool wasHovered = m_fetchButtonHovered;
    m_fetchButtonHovered = false;
    const int wasHoveredCategory = m_hoveredCategory;
    m_hoveredCategory = -1;

    if (cursor.isValid) {
        float mouseX = cursor.x;
        float mouseY = cursor.y;

        // Fetch button and list entry hover (click regions store base positions, add offset for hit testing)
        for (const auto& region : m_clickRegions) {
            if (region.type != ClickRegionType::FETCH_BUTTON &&
                region.type != ClickRegionType::CATEGORY_OPTION) continue;
            const bool over = isPointInRect(mouseX, mouseY,
                region.x + m_fOffsetX, region.y + m_fOffsetY, region.width, region.height);
            if (region.type == ClickRegionType::FETCH_BUTTON) m_fetchButtonHovered = over;
            else if (over) m_hoveredCategory = region.entry;
        }

        // Handle clicks; one anywhere but on this HUD closes an open list
        if (inputManager.getLeftButton().isClicked() && !isHeldBack() && !m_scroll.tookClick()) {
            if (ownsClick()) handleClick(mouseX, mouseY);
            else if (m_categoryOpen) closeCategoryList();
        }
    }
    // Repaint on any hover transition, including losing hover when the cursor
    // goes away (both were reset above).
    if (wasHovered != m_fetchButtonHovered || wasHoveredCategory != m_hoveredCategory) {
        setDataDirty();
    }

    // Check if fetch result display should timeout
    if (m_fetchState == FetchState::SUCCESS || m_fetchState == FetchState::FETCH_ERROR) {
        unsigned long elapsed = GetTickCount() - m_fetchResultTimestamp;
        if (elapsed > FETCH_RESULT_DISPLAY_MS) {
            // Keep showing the data, just reset state to IDLE
            m_fetchState = FetchState::IDLE;
            setDataDirty();
        }
    }

    // Check if cooldown just expired (need to re-enable button)
    {
        bool wasOnCooldown = m_wasOnCooldown;
        bool isOnCooldown = (GetTickCount() - m_fetchStartTimestamp < FETCH_COOLDOWN_MS);
        m_wasOnCooldown = isOnCooldown;
        if (wasOnCooldown && !isOnCooldown) {
            setDataDirty();
        }
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void RecordsHud::rebuildLayout() {
    // Rebuild everything for layout changes (dragging, scale, etc.)
    // Strings have offset baked in when created, so we need full rebuild
    rebuildRenderData();
}

void RecordsHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    m_overlayQuadFirst = m_overlayStringFirst = -1;   // set again by an open list

    // Get scaled dimensions
    auto dim = getScaledDimensions();

    // Copy ALL records for pagination (minimize mutex hold time)
    std::vector<RecordEntry> allRecords;
    std::string lastError;
    DataProvider recordsProvider;
    {
        MutexLock lock(m_recordsMutex);
        allRecords = m_records;  // Copy all for proper pagination
        lastError = m_lastError;
        recordsProvider = m_recordsProvider;  // Written by the fetch worker under this lock
    }
    // The table section's rows: the optional column-header row, the records,
    // and the footer note as a real last row.
    const int tableRows = (m_bShowHeaders ? 1 : 0) + m_recordsToShow
                        + (m_bShowFooter ? 1 : 0);

    // Calculate background width based on enabled columns
    // Note: padding is added by calculateBackgroundWidth(), don't double-count
    // Order: POS, RIDER, BIKE, SECTORS (S1/S2/S3), LAPTIME, DATE
    // Pure arithmetic, unit-tested in tests/unit/test_records_window.cpp -- including
    // WHY the last column keeps its gap, which is invisible in the numbers. A gap
    // after the last column reads as wasted width; it is that column's right
    // clearance inside the panel, and without it the lap time's final digit ends on
    // the same pixel as the player row's highlight band.
    const int bgWidthChars = RecordsWindow::backgroundWidthChars(
        { COL_POS_WIDTH, COL_RIDER_WIDTH, COL_BIKE_WIDTH,
          COL_SECTOR_WIDTH, COL_LAPTIME_WIDTH, COL_DATE_WIDTH },
        { (m_enabledColumns & COL_POS) != 0,     (m_enabledColumns & COL_RIDER) != 0,
          (m_enabledColumns & COL_BIKE) != 0,    (m_enabledColumns & COL_SECTORS) != 0,
          (m_enabledColumns & COL_LAPTIME) != 0, (m_enabledColumns & COL_DATE) != 0 },
        GAME_SECTOR_COUNT, MIN_WIDTH_CHARS);

    // BOX-MODEL: the selector row is its OWN section (the seam replaces the
    // old reserved empty row), the table is the second, and Compare is the
    // plan's footer BUTTON row rather than an inline chip on the selector row
    // -- the first HUD on the plan's button machinery.
    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(bgWidthChars, dim.fontSize);
    want.sectionH = { sectionHeadingRowHeight(dim) + dim.lineHeightNormal,
                      static_cast<float>(tableRows) * dim.lineHeightNormal };
    want.captionW = planTitleWidth(dim, TR("Records"), TitleTier::Large);
    want.tier = TitleTier::Large;
    want.buttons = 1;
    // "Compare" (7 chars) plus one char of air each side, as before; the
    // [button] border/padding wrap it via the plan.
    want.buttonW = PluginUtils::calculateMonospaceTextWidth(9, dim.fontSize);
    want.buttonH = dim.lineHeightNormal;
    PanelPlan& plan = planPanel(dim, want);
    float backgroundWidth = plan.width();
    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + plan.height());
    addPlanBackground(plan, START_X, START_Y);

    float contentStartX = plan.contentX();
    float currentY = plan.contentY();

    // Recalculate column positions based on enabled columns
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

    // === Title Row === (the plan's caption row, above currentY)
    addPlanTitle(plan, TR("Records"),
                 this->getColor(ColorSlot::PRIMARY));

    // === Filter section: heading row + the provider/category selectors ===
    // Note: Click regions store positions WITHOUT offset - offset is added during hit testing
    addFilterRow(contentStartX, currentY, dim);

    // The Compare button: the plan's footer button row.
    addCompareButton(plan, dim);

    // === Table section ===
    currentY = plan.contentY(1);

    // === Column header row (optional) ===
    if (m_bShowHeaders) {
        currentY = addColumnHeaders(currentY, dim);
    }

    // === Record Rows (with Personal Best integration) ===
    const float tableTop = currentY;
    RowCursor cur{plan, dim, currentY, 0};
    m_scrollListed = false;
    buildRecordRows(cur, allRecords, lastError, contentStartX);
    if (!m_scrollListed) m_scroll.layout(0, m_recordsToShow, 0);
    currentY = cur.y;
    int rowsRendered = cur.rowsRendered;


    // === Placeholder rows to fill up to configured size ===
    while (rowsRendered < m_recordsToShow) {
        // Empty row — background quad defines HUD extent
        if (isColumnEnabled(COL_POS)) {
            addString("", m_columns.pos, currentY, Justify::LEFT,
                      this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
        }
        currentY += dim.lineHeightNormal;
        rowsRendered++;
    }

    // The scrollbar in the panel's right padding, beside the record rows.
    m_scroll.draw(*this, plan.sectionBoxX() + plan.sectionBoxW(), plan.x0 + plan.width(),
                  tableTop, m_recordsToShow * dim.lineHeightNormal, dim.lineHeightNormal,
                  dim.fontSize);

    // === Footer Note: the table section's last row ===
    if (m_bShowFooter) {
        // "Submit by playing on <provider> servers" (small font, row height unchanged).
        // Rendered as ONE string so it can't misalign: a separately-positioned,
        // differently-colored provider segment placed by the monospace width estimate
        // breaks with a proportional NORMAL font (gap after "on", suffix jammed into
        // the provider name). A single left-justified string spaces correctly in any
        // font (the whole line is muted; the provider is not color-highlighted).
        char footer[128];
        snprintf(footer, sizeof(footer), TR("Submit by playing on %s servers"),
                 RecordsFetcher::getProviderDisplayName(recordsProvider));
        addString(footer, contentStartX, currentY,
                  Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSizeSmall);
    }

    // The open category list, last so it covers the rows under it.
    if (m_categoryOpen) {
        addCategoryList(backgroundWidth, plan.height());
    }
}

void RecordsHud::addCategoryList(float panelWidth, float panelHeight) {
    DropdownList::Entries entries;
    entries.count = static_cast<int>(m_categoryList.size());
    entries.current = m_categoryIndex;
    entries.hovered = m_hoveredCategory;
    entries.nameOf = [this](int i) { return i == 0 ? std::string(TR("All")) : m_categoryList[static_cast<size_t>(i)]; };
    std::vector<DropdownList::Rect> rects;
    DropdownList::build(*this, m_categoryAnchor,
        {START_X, START_Y, START_X + panelWidth, START_Y + panelHeight}, entries, dropdownStyle(), rects);
    // In front of the rows' regions: hit-testing takes the first match, and the
    // list covers the table (and, opened upwards, the selectors).
    for (size_t i = 0; i < rects.size(); ++i) {
        ClickRegion region{rects[i].x, rects[i].y, rects[i].w, rects[i].h, ClickRegionType::CATEGORY_OPTION};
        region.entry = static_cast<int>(i);
        m_clickRegions.insert(m_clickRegions.begin() + static_cast<std::ptrdiff_t>(i), region);
    }
}

DropdownList::Style RecordsHud::dropdownStyle() const {
    DropdownList::Style style;
    style.primary = getColor(ColorSlot::PRIMARY);
    style.secondary = getColor(ColorSlot::SECONDARY);
    style.muted = getColor(ColorSlot::MUTED);
    style.accent = getColor(ColorSlot::ACCENT);
    style.background = getColor(ColorSlot::BACKGROUND);
    style.font = getFont(FontCategory::NORMAL);
    return style;
}

// The provider/category selectors under the Filter heading.
void RecordsHud::addFilterRow(float contentStartX, float currentY, const ScaledDimensions& dim) {
    addSectionHeading(TR("Filter"), contentStartX, currentY, dim);
    currentY += sectionHeadingRowHeight(dim);
    float rowX = contentStartX;
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    // Provider selector: "< MXB Ranked >" - use fixed width so arrows don't jump
    static constexpr int PROVIDER_WIDTH_CHARS = 10;  // Longest: "MXB Ranked"
    float providerFixedWidth = PluginUtils::calculateMonospaceTextWidth(PROVIDER_WIDTH_CHARS, dim.fontSize);

    addString("<", rowX, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_LEFT});
    rowX += charWidth * 2;  // "< "

    addString(RecordsFetcher::getProviderDisplayName(m_provider), rowX, currentY, Justify::LEFT,
              this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    rowX += providerFixedWidth;  // Fixed width regardless of actual name length

    addString(">", rowX + charWidth, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Category selector: "< MX1-2T OEM v >" - use fixed width so arrows don't jump.
    // Seven categories, so the value is a dropdown box (the settings panel's, via
    // DropdownList) over the field and the caret's cell; the arrows still step.
    static constexpr int CATEGORY_WIDTH_CHARS = 10;  // Longest: "MX1-2T OEM"
    float categoryFixedWidth = PluginUtils::calculateMonospaceTextWidth(CATEGORY_WIDTH_CHARS, dim.fontSize);
    const float valueX = rowX + charWidth * 2;
    const float fieldW = categoryFixedWidth + charWidth;
    m_categoryAnchor = {valueX - charWidth * 0.3f, currentY, fieldW + charWidth * 0.3f, valueX,
                        dim.lineHeightNormal, dim.fontSize};
    // The box's region first: hit-testing takes the first match, and the arrows'
    // two-cell regions reach into the box's air and the caret's cell.
    m_clickRegions.push_back({m_categoryAnchor.x, currentY, m_categoryAnchor.w, dim.lineHeightNormal,
                              ClickRegionType::CATEGORY_BOX});
    DropdownList::drawBox(*this, valueX, currentY, fieldW, dim.lineHeightNormal, dim.fontSize, true,
                          m_categoryOpen, dropdownStyle());

    addString("<", rowX, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_LEFT});
    rowX += charWidth * 2;  // "< "

    const char* catName = getCurrentCategoryDisplay();
    addString(catName, rowX, currentY, Justify::LEFT,
              this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    rowX += categoryFixedWidth;  // Fixed width regardless of actual name length

    addString(">", rowX + charWidth, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_RIGHT});
    rowX += charWidth * 4;  // " > " + gap
}

void RecordsHud::addCompareButton(const PanelPlan& plan, const ScaledDimensions& dim) {
    // Compare button - fixed width (widest label "Compare" + 1ch padding each side); label centered
    // Button is disabled when trackName is unavailable or on cooldown
    const SessionData& sessionForButton = PluginData::getInstance().getSessionData();
    bool trackAvailable = sessionForButton.trackName[0] != '\0';
    bool isOnCooldown = (GetTickCount() - m_fetchStartTimestamp < FETCH_COOLDOWN_MS);
    FetchState state = m_fetchState.load();
    bool isButtonDisabled = !trackAvailable || (isOnCooldown && state != FetchState::FETCHING);

    const char* compareLabel = TR("Compare");
    if (state == FetchState::FETCHING) {
        compareLabel = "...";
    } else if (state == FetchState::SUCCESS) {
        compareLabel = TR("OK");
    } else if (state == FetchState::FETCH_ERROR) {
        compareLabel = TR("Error");
    }

    unsigned long compareColor;
    if (isButtonDisabled) {
        compareColor = this->getColor(ColorSlot::MUTED);  // Muted when disabled/cooldown
    } else if (state == FetchState::SUCCESS) {
        compareColor = this->getColor(ColorSlot::POSITIVE);
    } else if (state == FetchState::FETCH_ERROR) {
        compareColor = this->getColor(ColorSlot::NEGATIVE);
    } else {
        // Idle and fetching alike: whatever reads ON the accent chip -- see
        // BaseHud::chipGlyphColor(). Was "accent on accent", legible only because
        // every theme was dark. The three cases above are left alone on purpose:
        // those colours are the fetch RESULT, not decoration, and green/red on the
        // chip is the point.
        compareColor = this->buttonGlyphColor(this->getColor(ColorSlot::ACCENT));
    }

    // The plan's footer button row: box, art and label from the [button]
    // terms; the click region is the box.
    // Centred on the CARD rather than at the plan's left-aligned button
    // column — a single action under a table reads as the panel's, not the
    // first column's. The card, not the panel (PanelPlan::sectionBoxCenterX):
    // the two agree only while the [content] terms are left/right symmetric.
    const float btnW0 = plan.W(plan.g.btnW);
    const float btnX = plan.sectionBoxCenterX() - btnW0 / 2.0f;
    const float btnY = plan.Y(plan.g.btnTop);
    const float btnW = btnW0;
    const float btnH = plan.H(plan.g.btnH);
    if (!isButtonDisabled) {
        m_clickRegions.push_back({btnX, btnY, btnW, btnH, ClickRegionType::FETCH_BUTTON});
    }
    // Colour carries the state; addStateButton supplies the theme's button shape
    // (or a plain quad) and places the label. The glyph colour is OVERRIDDEN here
    // because it reports the fetch RESULT -- see compareColor above.
    addStateButton(btnX, btnY, btnW, btnH, compareLabel,
                   btnY + plan.H(plan.g.B.b.t + plan.g.B.p.t), dim.fontSize,
                   this->getColor(ColorSlot::ACCENT),
                   isButtonDisabled ? ButtonState::Disabled
                   : (m_fetchButtonHovered && state != FetchState::FETCHING)
                       ? ButtonState::Hovered : ButtonState::Idle,
                   compareColor);
}

float RecordsHud::addColumnHeaders(float currentY, const ScaledDimensions& dim) {
    unsigned long headerColor = this->getColor(ColorSlot::TERTIARY);
    if (isColumnEnabled(COL_POS))
        addLabel("P", m_columns.pos, currentY, Justify::LEFT, headerColor, dim);
    if (isColumnEnabled(COL_RIDER))
        addLabel(TR("Name"), m_columns.rider, currentY, Justify::LEFT, headerColor, dim);
    if (isColumnEnabled(COL_BIKE))
        addLabel(TR("Bike"), m_columns.bike, currentY, Justify::LEFT, headerColor, dim);
    if (isColumnEnabled(COL_SECTORS)) {
        addLabel("S1", m_columns.sector1, currentY, Justify::LEFT, headerColor, dim);
        addLabel("S2", m_columns.sector2, currentY, Justify::LEFT, headerColor, dim);
        addLabel("S3", m_columns.sector3, currentY, Justify::LEFT, headerColor, dim);
#if GAME_SECTOR_COUNT >= 4
        addLabel("S4", m_columns.sector4, currentY, Justify::LEFT, headerColor, dim);
#endif
    }
    if (isColumnEnabled(COL_LAPTIME))
        addLabel(TR("Time"), m_columns.laptime, currentY, Justify::LEFT, headerColor, dim);
    if (isColumnEnabled(COL_DATE))
        addLabel(TR("Date"), m_columns.date, currentY, Justify::LEFT, headerColor, dim);
    currentY += dim.lineHeightNormal;
    return currentY;
}

// ============================================================================
// Public API for TimingHud Integration
// ============================================================================

int RecordsHud::getFastestRecordLapTime() const {
    MutexLock lock(m_recordsMutex);
    if (m_records.empty()) return -1;
    // Records are sorted by lap time, first entry is fastest
    return m_records[0].laptime;
}

bool RecordsHud::getFastestRecordSectors(int& sector1, int& sector2, int& sector3, int& sector4) const {
    MutexLock lock(m_recordsMutex);
    if (m_records.empty()) return false;

    const auto& fastest = m_records[0];
    if (!fastest.hasSectors()) return false;

    sector1 = fastest.sector1;
    sector2 = fastest.sector2;
    sector3 = fastest.sector3;
    sector4 = fastest.sector4;
    return true;
}

void RecordsHud::resetToDefaults() {
    m_bVisible = false;
    m_categoryOpen = false;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    setPosition(DefaultSlots::rightColumnX(DefaultSlots::WIDE_CELLS), DefaultSlots::rightColumnY());
    m_provider = DataProvider::CBR;
    m_categoryIndex = 0;
    m_lastSessionTrackName[0] = '\0';  // Reset so update() will pick up current session track
    m_lastSessionCategory[0] = '\0';  // Reset so update() will pick up current session category
    m_bAutoFetch = false;  // Auto-fetch disabled by default
    m_bShowHeaders = false;  // Column headers off by default
    m_enabledColumns = COL_DEFAULT;
    m_recordsToShow = 8;  // Default to 8 rows (title 2 + 2 header + 8 + 1 footer + pad 2 = 15, matches StandingsHud)
    m_bShowFooter = true;
    {
        MutexLock lock(m_recordsMutex);
        m_records.clear();
        m_recordsProvider = DataProvider::CBR;  // Shared with the fetch worker - write under the lock
    }
    m_fetchState = FetchState::IDLE;
    setDataDirty();
}

// ============================================================================
// Test seams (MXBMRP3_TEST_BUILD only — see records_hud.h / core/test_hooks.cpp)
// ============================================================================

#if defined(MXBMRP3_TEST_BUILD)
bool RecordsHud::testParseResponse(int provider, const std::string& response) {
    // The REAL parse path (RecordsFetcher::parseResponse) with the provider a
    // startFetch() snapshot would carry, stored through the same
    // m_recordsMutex-guarded path the worker's completion uses.
    RecordsFetcher::Result result;
    DataProvider p = static_cast<DataProvider>(provider);
    if (!RecordsFetcher::parseResponse(p, response, result)) return false;
    result.parsed = true;
    result.provider = p;
    storeParsedRecords(std::move(result));
    return true;
}

void RecordsHud::testSetFetchStub(int delayMs, const char* response) {
    // The stub lives with the worker it stubs (core/records_fetcher.cpp);
    // forwarded so the exported hook name/shape is unchanged.
    RecordsFetcher::testSetFetchStub(delayMs, response);
}

int RecordsHud::testRecordCount() const {
    MutexLock lock(m_recordsMutex);
    return static_cast<int>(m_records.size());
}

bool RecordsHud::testGetRecord(int index, RecordEntry& out) const {
    MutexLock lock(m_recordsMutex);
    if (index < 0 || index >= static_cast<int>(m_records.size())) return false;
    out = m_records[index];
    return true;
}
#endif
