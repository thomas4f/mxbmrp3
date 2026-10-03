// ============================================================================
// hud/settings/settings_links.cpp
// The link rows: a click opens a browser and changes no setting.
//
// COMMON, NOT A TAB'S. The General tab has the Docs, Discussion and overlay
// rows, the About page the three thanks links, and About has no click handler
// of its own (its registry row routes clicks straight to dispatchRegion) -- so
// while these cases lived in handleClickTabGeneral, every About link fell
// through to the "Unknown ClickRegion" default and did nothing. dispatchRegion
// asks here before its own switch, for every tab.
//
// NO GATE CAN CLICK THEM: isPerturbSafe keeps the OPEN_LINK_* regions out of
// the click sweep precisely because they reach outside the game, so this is
// an in-game check.
// ============================================================================
#include "../settings_hud.h"
#include "../../game/game_config.h"
#if GAME_HAS_HTTP_SERVER
#include "../../core/http_server.h"      // OPEN_LINK_OVERLAY: the served port
#endif
#if GAME_HAS_ANALYTICS
#include "../../core/analytics_manager.h"
#endif

#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <string>

// True when the region was a link row (a browser was opened, or would have
// been); false for anything else, which the caller's own switch handles.
bool SettingsHud::handleLinkClick(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::OPEN_LINK_DOCS:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "docs"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://thomas4f.github.io/mxbmrp3", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case ClickRegion::OPEN_LINK_COMMUNITY:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "community"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://mxb-mods.com/mxbmrp3", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case ClickRegion::OPEN_LINK_KOFI:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "donate"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://ko-fi.com/thomas4f", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case ClickRegion::OPEN_LINK_GITHUB:
#if GAME_HAS_ANALYTICS
            AnalyticsManager::getInstance().trackEvent("link_clicked", {{"target", "github"}, {"source", "settings"}});
#endif
            ShellExecuteA(nullptr, "open", "https://github.com/thomas4f/mxbmrp3", nullptr, nullptr, SW_SHOWNORMAL);
            return true;

        case ClickRegion::OPEN_LINK_OVERLAY:
#if GAME_HAS_HTTP_SERVER
            {
                // The server's own address, built where it is shown -- not a constant,
                // because the port is a setting and a stale literal here would send the
                // user to a page that is not being served.
                const std::string url = "http://localhost:"
                    + std::to_string(HttpServer::getInstance().getPort());
                ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            return true;
#else
            return true;
#endif

        default:
            return false;
    }
}
