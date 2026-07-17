// ============================================================================
// hud/records_hud.cpp
// Displays lap records fetched from external data providers via HTTP
// ============================================================================
#include "records_hud.h"
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
    float scaledFontSize = FontSizes::NORMAL * scale;
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
    DEBUG_INFO("RecordsHud created");
    setDraggable(true);
    m_quads.reserve(2);  // Background + fetch button
    m_strings.reserve(60);  // Header rows + 10 record rows * 5 cols + footer
    m_records.reserve(MAX_RECORDS);
    m_clickRegions.reserve(5);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("records_hud");

    // Set all configurable defaults
    resetToDefaults();

    // Initialize column positions (after resetToDefaults sets m_enabledColumns)
    m_columns = ColumnPositions(START_X + Padding::HUD_HORIZONTAL, m_fScale, m_enabledColumns);

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
    m_categoryList.push_back("All");
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
        return m_categoryList[m_categoryIndex].c_str();
    }
    return "All";
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
            std::lock_guard<std::mutex> lock(m_recordsMutex);
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
    std::lock_guard<std::mutex> lock(m_recordsMutex);
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
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        if (!m_records.empty() || m_lastSessionTrackName[0] != '\0') {
            m_records.clear();
            m_fetchState = FetchState::IDLE;
            m_lastSessionTrackName[0] = '\0';  // Reset tracked track
            m_lastSessionCategory[0] = '\0';  // Reset tracked category
            setDataDirty();
        }
    } else {
        // Track changed (entered new event) - auto-fetch if enabled
        if (strcmp(session.trackName, m_lastSessionTrackName) != 0) {
            strncpy_s(m_lastSessionTrackName, sizeof(m_lastSessionTrackName), session.trackName, sizeof(m_lastSessionTrackName) - 1);
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
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

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

    if (cursor.isValid) {
        float mouseX = cursor.x;
        float mouseY = cursor.y;

        // Check for fetch button hover (click regions store base positions, add offset for hit testing)
        for (const auto& region : m_clickRegions) {
            if (region.type == ClickRegionType::FETCH_BUTTON) {
                m_fetchButtonHovered = isPointInRect(mouseX, mouseY,
                    region.x + m_fOffsetX, region.y + m_fOffsetY, region.width, region.height);
                break;
            }
        }

        // Handle clicks
        if (inputManager.getLeftButton().isClicked() && isPointInBounds(mouseX, mouseY)) {
            handleClick(mouseX, mouseY);
        }
    }
    // Repaint on any hover transition, including losing hover when the cursor
    // goes away (m_fetchButtonHovered was reset to false above).
    if (wasHovered != m_fetchButtonHovered) {
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

    // Get scaled dimensions
    auto dim = getScaledDimensions();
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Copy ALL records for pagination (minimize mutex hold time)
    std::vector<RecordEntry> allRecords;
    std::string lastError;
    DataProvider recordsProvider;
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        allRecords = m_records;  // Copy all for proper pagination
        lastError = m_lastError;
        recordsProvider = m_recordsProvider;  // Written by the fetch worker under this lock
    }
    int totalRecords = static_cast<int>(allRecords.size());
    int footerRows = m_bShowFooter ? FOOTER_ROWS : 0;
    int totalRows = HEADER_ROWS + m_recordsToShow + footerRows;

    // Calculate background width based on enabled columns
    // Note: padding is added by calculateBackgroundWidth(), don't double-count
    // Order: POS, RIDER, BIKE, SECTORS (S1/S2/S3), LAPTIME, DATE
    int bgWidthChars = 0;
    if (m_enabledColumns & COL_POS) bgWidthChars += COL_POS_WIDTH;
    if (m_enabledColumns & COL_RIDER) bgWidthChars += COL_RIDER_WIDTH;
    if (m_enabledColumns & COL_BIKE) bgWidthChars += COL_BIKE_WIDTH;
    if (m_enabledColumns & COL_SECTORS) bgWidthChars += COL_SECTOR_WIDTH * GAME_SECTOR_COUNT;  // S1, S2, S3 (and S4 for 4-sector games)
    if (m_enabledColumns & COL_LAPTIME) bgWidthChars += COL_LAPTIME_WIDTH;
    if (m_enabledColumns & COL_DATE) bgWidthChars += COL_DATE_WIDTH;
    // Remove trailing gap from last visible column (gap not needed after last column)
    // Find the last enabled column and subtract 1 for its gap
    uint32_t lastCol = 0;
    if (m_enabledColumns & COL_DATE) lastCol = COL_DATE;
    else if (m_enabledColumns & COL_LAPTIME) lastCol = COL_LAPTIME;
    else if (m_enabledColumns & COL_SECTORS) lastCol = COL_SECTORS;
    else if (m_enabledColumns & COL_BIKE) lastCol = COL_BIKE;
    else if (m_enabledColumns & COL_RIDER) lastCol = COL_RIDER;
    if (lastCol != 0 && lastCol != COL_DATE) {
        bgWidthChars -= 1;  // Remove gap from last column (unless it's DATE which has no gap anyway)
    }
    bgWidthChars = std::max(bgWidthChars, 34);  // Minimum width for controls row

    float backgroundWidth = calculateBackgroundWidth(bgWidthChars);
    float backgroundHeight = calculateBackgroundHeight(totalRows - 1);

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    float contentStartY = START_Y + dim.paddingV;
    float currentY = contentStartY;

    // Recalculate column positions based on enabled columns
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

    // === Title Row ===
    addTitleString("Records", contentStartX, currentY, Justify::LEFT,
                   this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    currentY += titleHeight;

    // === Provider / Category / Fetch Row ===
    // Note: Click regions store positions WITHOUT offset - offset is added during hit testing
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

    addString(" >", rowX, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Category selector: "< MX1-2T OEM >" - use fixed width so arrows don't jump
    static constexpr int CATEGORY_WIDTH_CHARS = 10;  // Longest: "MX1-2T OEM"
    float categoryFixedWidth = PluginUtils::calculateMonospaceTextWidth(CATEGORY_WIDTH_CHARS, dim.fontSize);

    addString("<", rowX, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_LEFT});
    rowX += charWidth * 2;  // "< "

    const char* catName = getCurrentCategoryDisplay();
    addString(catName, rowX, currentY, Justify::LEFT,
              this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    rowX += categoryFixedWidth;  // Fixed width regardless of actual name length

    addString(" >", rowX, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::ACCENT), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Compare button - fixed width (widest label "Compare" + 1ch padding each side); label centered
    // Button is disabled when trackName is unavailable or on cooldown
    const SessionData& sessionForButton = PluginData::getInstance().getSessionData();
    bool trackAvailable = sessionForButton.trackName[0] != '\0';
    bool isOnCooldown = (GetTickCount() - m_fetchStartTimestamp < FETCH_COOLDOWN_MS);
    FetchState state = m_fetchState.load();
    bool isButtonDisabled = !trackAvailable || (isOnCooldown && state != FetchState::FETCHING);

    const char* compareLabel = "Compare";
    if (state == FetchState::FETCHING) {
        compareLabel = "...";
    } else if (state == FetchState::SUCCESS) {
        compareLabel = "OK";
    } else if (state == FetchState::FETCH_ERROR) {
        compareLabel = "Error";
    }

    unsigned long compareColor;
    if (isButtonDisabled) {
        compareColor = this->getColor(ColorSlot::MUTED);  // Muted when disabled/cooldown
    } else if (state == FetchState::SUCCESS) {
        compareColor = this->getColor(ColorSlot::POSITIVE);
    } else if (state == FetchState::FETCH_ERROR) {
        compareColor = this->getColor(ColorSlot::NEGATIVE);
    } else if (state == FetchState::FETCHING) {
        // Always accent during fetch (no hover feedback since button is busy)
        compareColor = this->getColor(ColorSlot::ACCENT);
    } else {
        // Use PRIMARY when hovered, ACCENT when not (purple on purple)
        compareColor = m_fetchButtonHovered ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::ACCENT);
    }

    float compareWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen("Compare")) + 2, dim.fontSize);

    // Only add click region if button is enabled
    if (!isButtonDisabled) {
        m_clickRegions.push_back({rowX, currentY, compareWidth, dim.lineHeightNormal, ClickRegionType::FETCH_BUTTON});
    }

    // Button background - muted when disabled, accent when enabled
    {
        SPluginQuad_t bgQuad;
        float bgX = rowX;
        float bgY = currentY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, compareWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        if (isButtonDisabled) {
            bgQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 64.0f / 255.0f);
        } else {
            bgQuad.m_ulColor = m_fetchButtonHovered && state != FetchState::FETCHING
                ? this->getColor(ColorSlot::ACCENT)
                : PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), 128.0f / 255.0f);
        }
        m_quads.push_back(bgQuad);
    }

    addString(compareLabel, rowX + compareWidth / 2.0f, currentY, Justify::CENTER, this->getFont(FontCategory::NORMAL), compareColor, dim.fontSize);

    currentY += dim.lineHeightNormal;

    // === Column header row (optional) ===
    // HEADER_ROWS always reserves this row; when headers are off it stays empty.
    if (m_bShowHeaders) {
        unsigned long headerColor = this->getColor(ColorSlot::TERTIARY);
        int headerFont = this->getFont(FontCategory::STRONG);
        if (isColumnEnabled(COL_POS))
            addLabel("Pos", m_columns.pos, currentY, Justify::LEFT, headerFont, headerColor, dim);
        if (isColumnEnabled(COL_RIDER))
            addLabel("Rider", m_columns.rider, currentY, Justify::LEFT, headerFont, headerColor, dim);
        if (isColumnEnabled(COL_BIKE))
            addLabel("Bike", m_columns.bike, currentY, Justify::LEFT, headerFont, headerColor, dim);
        if (isColumnEnabled(COL_SECTORS)) {
            addLabel("S1", m_columns.sector1, currentY, Justify::LEFT, headerFont, headerColor, dim);
            addLabel("S2", m_columns.sector2, currentY, Justify::LEFT, headerFont, headerColor, dim);
            addLabel("S3", m_columns.sector3, currentY, Justify::LEFT, headerFont, headerColor, dim);
#if GAME_SECTOR_COUNT >= 4
            addLabel("S4", m_columns.sector4, currentY, Justify::LEFT, headerFont, headerColor, dim);
#endif
        }
        if (isColumnEnabled(COL_LAPTIME))
            addLabel("Time", m_columns.laptime, currentY, Justify::LEFT, headerFont, headerColor, dim);
        if (isColumnEnabled(COL_DATE))
            addLabel("Date", m_columns.date, currentY, Justify::LEFT, headerFont, headerColor, dim);
    }
    currentY += dim.lineHeightNormal;

    // === Record Rows (with Personal Best integration) ===
    // Track how many rows we render so we can fill with placeholders
    int rowsRendered = 0;

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

    // Lambda to render a single record row
    // isPlayerRow: add highlight background, skip position column
    // sector1/2/3/4: -1 if not available (CBR or player PB)
    auto renderRecordRow = [&](int position, const char* rider, const char* bike, int laptime,
                               int sector1, int sector2, int sector3, int sector4, const char* date, bool isPlayerRow) {
        // Add highlight background quad for player row
        if (isPlayerRow) {
            SPluginQuad_t highlight;
            float highlightX = START_X;
            float highlightY = currentY;
            applyOffset(highlightX, highlightY);
            setQuadPositions(highlight, highlightX, highlightY, backgroundWidth, dim.lineHeightNormal);
            highlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            highlight.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), 80.0f / 255.0f);
            m_quads.push_back(highlight);
        }

        // Position (P1, P2, etc.) - skip for player row
        if (isColumnEnabled(COL_POS) && !isPlayerRow) {
            char posStr[8];
            snprintf(posStr, sizeof(posStr), "P%d", position);
            unsigned long posColor;
            if (position == 1) posColor = PodiumColors::GOLD;
            else if (position == 2) posColor = PodiumColors::SILVER;
            else if (position == 3) posColor = PodiumColors::BRONZE;
            else posColor = this->getColor(ColorSlot::PRIMARY);
            addString(posStr, m_columns.pos, currentY, Justify::LEFT, this->getFont(FontCategory::NORMAL), posColor, dim.fontSize);
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
                PluginUtils::formatSectorTime(sector1, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector1, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector1, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
            }

            // S2
            if (sector2 > 0) {
                PluginUtils::formatSectorTime(sector2, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector2, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector2, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
            }

            // S3
            if (sector3 > 0) {
                PluginUtils::formatSectorTime(sector3, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector3, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector3, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
            }

#if GAME_SECTOR_COUNT >= 4
            // S4 (4-sector games only)
            if (sector4 > 0) {
                PluginUtils::formatSectorTime(sector4, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector4, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector4, currentY, Justify::LEFT,
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
                addString(Placeholders::LAP_TIME, m_columns.laptime, currentY, Justify::LEFT,
                          this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
            }
        }

        // Date
        if (isColumnEnabled(COL_DATE)) {
            addString(date && date[0] != '\0' ? date : "---", m_columns.date, currentY,
                      Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
        rowsRendered++;
    };

    FetchState currentState = m_fetchState.load();
    bool hasFetched = (currentState == FetchState::SUCCESS || !allRecords.empty());

    // Format player's PB date from timestamp (used in all branches)
    const char* playerName = session.riderName[0] != '\0' ? session.riderName : "You";
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
        // Before fetch or error: show player's PB and/or status message
        if (hasPlayerPB) {
            renderRecordRow(1, playerName, playerPBBike, playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
        }
        // Show status message (error or prompt to compare) - counts as a row to maintain layout
        const char* statusMessage = nullptr;
        char errorMessage[64];
        if (currentState == FetchState::FETCH_ERROR) {
            if (!lastError.empty()) {
                snprintf(errorMessage, sizeof(errorMessage), "Compare failed: %s", lastError.c_str());
            } else {
                strncpy_s(errorMessage, sizeof(errorMessage), "Compare failed. Try again.", sizeof(errorMessage) - 1);
            }
            statusMessage = errorMessage;
        } else if (!hasPlayerPB) {
            statusMessage = "Click Compare to load records.";
        }
        if (statusMessage) {
            addString(statusMessage, contentStartX, currentY,
                      Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
            currentY += dim.lineHeightNormal;
            rowsRendered++;
        }
    } else if (allRecords.empty()) {
        // Fetched but no records found - show player's PB and/or message
        if (hasPlayerPB) {
            renderRecordRow(1, playerName, playerPBBike, playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, playerPB->sector4, playerDateStr, true);
        }
        // Show "no records" message - counts as a row to maintain layout
        addString("No records found for this track/category.", contentStartX, currentY,
                  Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
        currentY += dim.lineHeightNormal;
        rowsRendered++;
    } else {
        // Has records - show with StandingsHud-style pagination
        // Strategy (like StandingsHud):
        // - If player is in top 3 (or no PB): show first N records with player inserted
        // - If player is beyond top 3: show top 3, then context around player position

        static constexpr int TOP_POSITIONS = 3;

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

            // 2. Calculate context window around player
            // Available rows = total - top3 - 1 (for player row)
            int availableRows = m_recordsToShow - TOP_POSITIONS - 1;
            int contextStart, contextEnd;

            if (playerPosition >= totalRecords) {
                // Player is slower than all fetched records - show bottom N records before them
                contextEnd = totalRecords - 1;
                contextStart = std::max(TOP_POSITIONS, totalRecords - availableRows);
            } else {
                // Player is within the records - show context around them
                int contextBefore = availableRows / 2;
                int contextAfter = availableRows - contextBefore - 1;

                contextStart = std::max(TOP_POSITIONS, playerPosition - contextBefore);
                contextEnd = std::min(totalRecords - 1, playerPosition + contextAfter);

                // Adjust if we hit boundaries - shift the window to use all available rows
                if (contextEnd == totalRecords - 1 && contextStart > TOP_POSITIONS) {
                    // Hit bottom - extend start upward to fill rows
                    contextStart = std::max(TOP_POSITIONS, contextEnd - availableRows + 1);
                } else if (contextStart == TOP_POSITIONS && contextEnd < totalRecords - 1) {
                    // Hit top (right after top 3) - extend end downward to fill rows
                    contextEnd = std::min(totalRecords - 1, contextStart + availableRows - 1);
                }
            }

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

    // === Footer Note (rendered in bottom padding area) ===
    if (m_bShowFooter) {
        // Position in bottom padding: after all content rows + gap row
        currentY = contentStartY + titleHeight + ((HEADER_ROWS - 1 + m_recordsToShow + 1) * dim.lineHeightNormal);

        // "Submit by playing on <provider> servers" (small font, row height unchanged).
        // Rendered as ONE string so it can't misalign: the provider used to be a
        // separately-positioned, differently-colored segment placed by the monospace
        // width estimate, which broke with a proportional NORMAL font (gap after "on",
        // suffix jammed into the provider name). A single left-justified string spaces
        // correctly in any font (the whole line is muted; the provider is no longer
        // color-highlighted).
        char footer[128];
        snprintf(footer, sizeof(footer), "Submit by playing on %s servers",
                 RecordsFetcher::getProviderDisplayName(recordsProvider));
        addString(footer, contentStartX, currentY,
                  Justify::LEFT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSizeSmall);
    }
}

// ============================================================================
// Public API for TimingHud Integration
// ============================================================================

int RecordsHud::getFastestRecordLapTime() const {
    std::lock_guard<std::mutex> lock(m_recordsMutex);
    if (m_records.empty()) return -1;
    // Records are sorted by lap time, first entry is fastest
    return m_records[0].laptime;
}

bool RecordsHud::getFastestRecordSectors(int& sector1, int& sector2, int& sector3, int& sector4) const {
    std::lock_guard<std::mutex> lock(m_recordsMutex);
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
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.7315f, 0.48107f);
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
        std::lock_guard<std::mutex> lock(m_recordsMutex);
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
    std::lock_guard<std::mutex> lock(m_recordsMutex);
    return static_cast<int>(m_records.size());
}

bool RecordsHud::testGetRecord(int index, RecordEntry& out) const {
    std::lock_guard<std::mutex> lock(m_recordsMutex);
    if (index < 0 || index >= static_cast<int>(m_records.size())) return false;
    out = m_records[index];
    return true;
}
#endif
