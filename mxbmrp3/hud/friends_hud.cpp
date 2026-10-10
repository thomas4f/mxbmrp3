// ============================================================================
// hud/friends_hud.cpp
// Friends HUD - renders the SteamFriendsManager roster as a table.
// ============================================================================
#include "friends_hud.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"
#include "../core/steam_friends_manager.h"
#include "../core/asset_manager.h"
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>

using namespace PluginConstants;

// Truncation is shared: PluginUtils::fitText (UTF-8 aware, ellipsis within budget).

FriendsHud::FriendsHud() {
    setDraggable(true);
    // Body card: this HUD draws a content BLOCK under its title, which is what the
    // themed card frames. Opt-in; see BaseHud::m_bContentCard.
    m_bContentCard = true;
    m_quads.reserve(1);
    m_strings.reserve(1 + m_maxDisplayRows * 7);  // title + up to 7 cells/row
    setTextureBaseName("friends_hud");
    resetToDefaults();
    rebuildRenderData();
}

bool FriendsHud::handlesDataType(DataChangeType dataType) const {
    // The manager refreshes its roster on these coarse changes; rebuild so we pull
    // the latest snapshot.
    return (dataType == DataChangeType::SessionData ||
            dataType == DataChangeType::Standings ||
            dataType == DataChangeType::SpectateTarget);
}

void FriendsHud::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // The empty panel names Steam's state (off / not available), and neither a
    // toggle in Settings > General nor a late hook is a data change this HUD is
    // told about -- so watch the pair. Two plain loads per frame.
    const SteamFriendsManager& steam = SteamFriendsManager::getInstance();
    const bool steamEnabled = steam.isEnabled();
    const int steamStatus = static_cast<int>(steam.getStatus());
    if (steamEnabled != m_lastSteamEnabled || steamStatus != m_lastSteamStatus) {
        m_lastSteamEnabled = steamEnabled;
        m_lastSteamStatus = steamStatus;
        setDataDirty();
    }

    // ON_JOIN: show for a window after friend activity (manager-detected: a friend
    // coming in-game or switching servers - never the user's own session/timing).
    // Runs every frame; flips dirty only on the show<->hide transition so the
    // render gate in rebuildRenderData re-evaluates and shows/hides the box.
    if (m_showMode == ShowMode::ON_JOIN) {
        const auto activity = SteamFriendsManager::getInstance().getLastActivityTime();
        const bool showing = (activity.time_since_epoch().count() != 0) &&
            (std::chrono::steady_clock::now() - activity < std::chrono::milliseconds(m_onJoinDurationMs));
        if (showing != m_activityShowing) {
            m_activityShowing = showing;
            setDataDirty();
        }
    }

    processDirtyFlags();
}

const char* FriendsHud::getShowModeName(ShowMode mode) {
    switch (mode) {
        case ShowMode::ALWAYS:       return "Always";
        case ShowMode::WITH_FRIENDS: return "Friends";
        case ShowMode::ON_JOIN:      return "On join";
        default:                     return "Unknown";
    }
}

int FriendsHud::getBackgroundWidthChars() const {
    int w = COL_BADGE_W + COL_NAME_W;  // Name + badge always shown
    if (isColumnEnabled(COL_SERVER)) w += COL_SERVER_W;
    if (isColumnEnabled(COL_TRACK))  w += COL_TRACK_W;
    if (isColumnEnabled(COL_INFO))   w += COL_INFO_W;
    if (isColumnEnabled(COL_TIMER))  w += COL_TIMER_W;
    return w;
}

void FriendsHud::rebuildLayout() {
    // Left-justified text columns; positions bake in on creation, so a
    // position-only fast path buys little. Rebuild everything (cheap).
    rebuildRenderData();
}

void FriendsHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Resolve the same-server badge icon once (assets are discovered before HUDs
    // render). m_badgeIcon <= 0 means it wasn't found -> the row falls back to a dot.
    if (!m_badgeIconResolved) {
        m_badgeIcon = AssetManager::getInstance().getIconSpriteIndex("circle");
        m_badgeIconResolved = true;
    }

    // Snapshot the roster - the manager owns ingestion + the same-server flag.
    // BY VALUE, and it must outlive `view` below, which holds pointers into it:
    // the scan runs on a worker thread and rewrites the manager's copy.
    const std::vector<SteamFriend> roster = SteamFriendsManager::getInstance().getFriends();

    // Order: same-server, then online (other server), then offline-with-data,
    // then keyless/unknown; alphabetical within each group.
    std::vector<const SteamFriend*> view;
    view.reserve(roster.size());
    for (const SteamFriend& f : roster) view.push_back(&f);
    std::sort(view.begin(), view.end(), [](const SteamFriend* a, const SteamFriend* b) {
        auto group = [](const SteamFriend* f) -> int {
            if (f->sameServer)        return 0;  // on your server + track
            if (!f->server.empty())   return 1;  // in a session (server / Testing / Unknown)
            if (f->hasData)           return 2;  // in menus
            return 3;                            // in the game, no plugin data
        };
        const int ga = group(a), gb = group(b);
        if (ga != gb) return ga < gb;
        return a->name < b->name;
    });

    // "Show myself": pin our own presence row at the top. getSelf() is a stable
    // member of the manager, kept current by updateLocalPresence(); only valid
    // once connected (and pointless when the integration is off).
    if (m_showSelf) {
        const SteamFriendsManager& mgr = SteamFriendsManager::getInstance();
        if (mgr.isEnabled() && mgr.getStatus() == SteamFriendsManager::Status::CONNECTED) {
            view.insert(view.begin(), &mgr.getSelf());
        }
    }

    const bool empty = view.empty();

    // Nothing to list -> a notice in place of the table, the shape the Gamepad and
    // Stream Chat panels use (BaseHud::addBlockedNotice): the panel keeps the size the
    // table will have, and the notice says why it is empty. Steam switched off or
    // unreachable outranks an empty roster, because nothing will ever fill the table
    // until it is fixed -- so it reads NEGATIVE with a hint naming the fix, like
    // "Controller N Not Connected" and "Stream chat off". A real empty roster is not a
    // fault: MUTED, no hint.
    const char* kTitle = "Friends";
    const char* noticeMsg = nullptr;
    const char* noticeHint = nullptr;
    ColorSlot noticeSlot = ColorSlot::MUTED;
    if (empty) {
        const SteamFriendsManager& mgr = SteamFriendsManager::getInstance();
        if (!mgr.isEnabled()) {
            noticeMsg  = "Steam integration off";
            noticeHint = "Check MXBMRP3 Settings > General";
            noticeSlot = ColorSlot::NEGATIVE;
        } else if (mgr.getStatus() != SteamFriendsManager::Status::CONNECTED) {
            noticeMsg  = "Steam not available";
            noticeHint = "Launch the game via Steam";
            noticeSlot = ColorSlot::NEGATIVE;
        } else {
            noticeMsg  = "No friends in-game";
        }
    }

    // Show-mode gate: the Visible toggle is the master on/off; this decides
    // *when* to actually display while enabled. When a mode hides the box it's
    // fully hidden (like other auto-hiding HUDs) - use "Always" to reposition it.
    const bool friendsPresent = !SteamFriendsManager::getInstance().getFriends().empty();
    bool display = true;
    switch (m_showMode) {
        case ShowMode::ALWAYS:       display = true;            break;
        case ShowMode::WITH_FRIENDS: display = friendsPresent;  break;
        case ShowMode::ON_JOIN:      display = m_activityShowing; break;
        default:                     display = true;            break;
    }
    // ...unless the Friends tab is open (isPreviewing): "With friends" and "On
    // join" both leave the panel off screen most of the time, which is when it has
    // to be positioned. It stands in with exactly what "Always" would draw -- the
    // roster, or the notice at the table's full size -- so the preview IS the live
    // panel, and a Steam problem is never hidden behind it. No fake friend anywhere.
    if (!display && !isPreviewing()) {
        setBounds(START_X, START_Y, START_X, START_Y);
        return;
    }

    // The empty panel takes the table's FULL configured size (header row + every
    // row), the most it will ever need, so it does not grow under the player when
    // the first friend arrives -- the Event Log's empty panel does the same.
    const int rowsToShow = empty ? m_maxDisplayRows : std::min(static_cast<int>(view.size()), m_maxDisplayRows);

    // BOX-MODEL: the caption's ask covers the title's width (widest-ask rule),
    // so the empty box need not hand-max it in; the standard-width floor
    // rides as the panel minimum. The notice widens the box only if a column
    // set is narrower than its text.
    const float headerHeight = m_bShowHeaders ? dim.lineHeightNormal : 0.0f;
    const float tableW = PluginUtils::calculateMonospaceTextWidth(getBackgroundWidthChars(), dim.fontSize);
    PanelWant want;
    want.contentW = empty
        ? std::max({ tableW,
                     PluginUtils::calculateMonospaceTextWidth(static_cast<int>(std::strlen(noticeMsg)), dim.fontSize),
                     noticeHint ? PluginUtils::calculateMonospaceTextWidth(
                         static_cast<int>(std::strlen(noticeHint)), dim.fontSize * 0.8f) : 0.0f })
        : tableW;
    want.sectionH = { headerHeight + dim.lineHeightNormal * rowsToShow };
    want.captionW = planTitleWidth(dim, kTitle, TitleTier::Large);
    want.tier = TitleTier::Large;
    want.minPanelW = PluginUtils::calculateMonospaceTextWidth(MIN_WIDTH_CHARS, dim.fontSize);
    PanelPlan& plan = planPanel(dim, want);
    const float backgroundWidth = plan.width();

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + plan.height());
    addPlanBackground(plan, START_X, START_Y);

    float currentY = plan.contentY();

    addPlanTitle(plan, kTitle, getColor(ColorSlot::PRIMARY));

    if (empty) {
        // Centred in the content box (header row included: with nothing listed there
        // is nothing for the headers to caption).
        addBlockedNotice(plan.contentX() + plan.contentW() * 0.5f,
                         currentY + want.sectionH[0] * 0.5f,
                         dim.lineHeightNormal, dim.fontSize, noticeMsg, noticeSlot, noticeHint);
        return;
    }

    // Column X offsets (left-justified). Only enabled columns consume space, so
    // disabling a column collapses the ones to its right. Columns start at the
    // plan's content origin.
    const float contentStartX = plan.contentX();
    float x = contentStartX;
    auto advance = [&](int chars) -> float {
        const float col = x;
        x += PluginUtils::calculateMonospaceTextWidth(chars, dim.fontSize);
        return col;
    };
    const float xBadge  = advance(COL_BADGE_W);
    const float xName   = advance(COL_NAME_W);
    const float xServer = isColumnEnabled(COL_SERVER) ? advance(COL_SERVER_W) : -1.0f;
    const float xTrack  = isColumnEnabled(COL_TRACK)  ? advance(COL_TRACK_W)  : -1.0f;
    const float xInfo   = isColumnEnabled(COL_INFO)   ? advance(COL_INFO_W)   : -1.0f;
    const float xTimer  = isColumnEnabled(COL_TIMER)  ? advance(COL_TIMER_W)  : -1.0f;

    const int nameFont   = getFont(FontCategory::NORMAL);
    const unsigned long colName   = getColor(ColorSlot::PRIMARY);
    const unsigned long colData   = getColor(ColorSlot::SECONDARY);
    const unsigned long colMuted  = getColor(ColorSlot::MUTED);
    const unsigned long colBadge  = getColor(ColorSlot::POSITIVE);
    const unsigned long colHeader = getColor(ColorSlot::TERTIARY);

    if (m_bShowHeaders) {
        // addLabel: Small font size, row-centered - the column-header
        // convention used by StandingsHud/LapLogHud/etc.
        addLabel("Name", xName, currentY, Justify::LEFT, colHeader, dim);
        if (xServer >= 0) addLabel("Server", xServer, currentY, Justify::LEFT, colHeader, dim);
        if (xTrack  >= 0) addLabel("Track",  xTrack,  currentY, Justify::LEFT, colHeader, dim);
        if (xInfo   >= 0) addLabel("Info",   xInfo,   currentY, Justify::LEFT, colHeader, dim);
        if (xTimer  >= 0) addLabel("Timer",  xTimer,  currentY, Justify::LEFT, colHeader, dim);
        currentY += dim.lineHeightNormal;
    }

    const char* DASH = Placeholders::GENERIC;  // "-"

    for (int i = 0; i < rowsToShow; ++i) {
        const SteamFriend& f = *view[i];
        const bool online = !f.server.empty();

        // Same-server badge: a tinted "circle" icon sprite (the StandingsHud
        // approach), since a font glyph like U+25CF isn't reliably rendered.
        // Sized + placed exactly like StandingsHud's status icons: 0.006 half-size
        // scaled by m_fScale, a quarter into the column, vertically row-centered.
        // Falls back to a solid dot if the icon isn't available.
        if (f.sameServer) {
            constexpr float BADGE_FULL_SIZE = 0.012f;  // 0.006 half-size, matches StandingsHud
            const float badgeColW = PluginUtils::calculateMonospaceTextWidth(COL_BADGE_W, dim.fontSize);
            const float cx = xBadge + badgeColW * 0.25f;
            const float cy = currentY + dim.lineHeightNormal * 0.5f;
            if (m_badgeIcon > 0) addIcon(cx, cy, m_badgeIcon, colBadge, BADGE_FULL_SIZE * m_fScale);
            else                 addDot(cx, cy, colBadge, BADGE_FULL_SIZE * m_fScale * 0.7f);
        }

        // Name (always).
        addString(PluginUtils::fitText(f.name, COL_NAME_W - 1).c_str(), xName, currentY, Justify::LEFT, nameFont, colName, dim.fontSize);

        // Server = "where": the server label (name / "Testing" solo / "Unknown")
        // when in a session, else the friend's status ("In Menus", or "Unknown"
        // when in the game with no plugin data). Always populated.
        if (xServer >= 0) {
            if (online) {
                addString(PluginUtils::fitText(f.server, COL_SERVER_W - 1).c_str(), xServer, currentY, Justify::LEFT, nameFont, colData, dim.fontSize);
            } else {
                const char* ind = !f.status.empty() ? SteamFriendsManager::LABEL_IN_MENUS : SteamFriendsManager::LABEL_UNKNOWN;
                addString(PluginUtils::fitText(ind, COL_SERVER_W - 1).c_str(), xServer, currentY, Justify::LEFT, nameFont, colMuted, dim.fontSize);
            }
        }

        // Track: the track name, or a dash when the friend isn't on one. Their
        // "In Menus" / "Unknown" status is in the Server column; Track holds only a
        // track name.
        if (xTrack >= 0) {
            if (!f.track.empty()) {
                addString(PluginUtils::fitText(f.track, COL_TRACK_W - 1).c_str(), xTrack, currentY, Justify::LEFT, nameFont, colData, dim.fontSize);
            } else {
                addString(DASH, xTrack, currentY, Justify::LEFT, nameFont, colMuted, dim.fontSize);
            }
        }

        // Info: "Session (Format), State" (empties skipped), or a dash when the
        // friend isn't in a session. The session name is dropped when it already
        // shows as the server label (solo "Testing"), so it isn't repeated.
        if (xInfo >= 0) {
            std::string info;
            if (!f.track.empty() || !f.session.empty()) {
                auto add = [&](const std::string& part) {
                    if (part.empty()) return;
                    if (!info.empty()) info += ", ";
                    info += part;
                };
                std::string sess = (f.session != f.server) ? f.session : std::string();
                if (!f.format.empty()) {
                    sess = sess.empty() ? ("(" + f.format + ")") : (sess + " (" + f.format + ")");
                }
                add(sess);
                add(f.state);
            }
            if (info.empty()) {
                addString(DASH, xInfo, currentY, Justify::LEFT, nameFont, colMuted, dim.fontSize);
            } else {
                addString(PluginUtils::fitText(info, COL_INFO_W - 1).c_str(), xInfo, currentY, Justify::LEFT,
                          nameFont, colData, dim.fontSize);
            }
        }

        // Timer - the friend's session clock (MM:SS, or N TO GO / FINAL LAP /
        // CHECKERED in time+lap overtime). A coarse snapshot, not a live tick.
        // Digits font, like the Timing panel's Time readout (same formatSessionClock text).
        if (xTimer >= 0) {
            const bool has = !f.progress.empty();
            addString(has ? PluginUtils::fitText(f.progress, COL_TIMER_W - 1).c_str() : DASH,
                      xTimer, currentY, Justify::LEFT, getFont(FontCategory::DIGITS), has ? colData : colMuted, dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
    }
}

void FriendsHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    setPosition(cellsX(133), cellsY(47));  // right-column tower, in settings order (after Event Log, before FMX)
    m_enabledColumns = COL_DEFAULT;
    m_maxDisplayRows = 8;
    m_bShowHeaders = true;
    m_showMode = ShowMode::WITH_FRIENDS;
    m_onJoinDurationMs = 15000;
    m_showSelf = true;
    setDataDirty();
}
