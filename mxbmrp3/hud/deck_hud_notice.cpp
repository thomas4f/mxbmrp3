// ============================================================================
// hud/deck_hud_notice.cpp
// The Notice deck (deck_hud.h): what the plugin tells the player, through
// one queue. Two feeds meet here, each stepped once per frame by HudManager
// before any HUD reads it: the race notices (NoticeTracker) and the toasts,
// achievements and the plugin's own messages (ToastFeed).
//
// THE RULE, one notice or toast at a time:
//  - a notice that goes ahead of toasts (NoticeTracker::aboveToasts: WRONG
//    WAY, BLUE FLAG, a PB...) shows first; a toast taken meanwhile is held,
//    its clock stopped (HudManager steps ToastFeed masked), not lost;
//  - else the toast;
//  - else the race's standing notice (LAST LAP, FINISHED, DEFAULT SETUP);
//  - else nothing: no bounds, no click. With the Decks tab open, a preview.
// The hide-all hotkey lets through only its own message, as on the toast card.
//
// The tab names the source (ALERT, LAP, RACE, SETUP, MUSIC, ACHIEVEMENT,
// MESSAGE), the stamp counts what waits behind the one showing ("+2"), and
// the note, the icon, the tab and the glow take the message's own colour: the
// Notices slab's for a notice, the tier's metal for an achievement.
// ============================================================================
#include "deck_hud.h"
#include "deck_format.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../core/asset_manager.h"
#include "../core/hud_manager.h"
#include "../core/i18n.h"
#include "../core/input_manager.h"
#include "../core/plugin_constants.h"
#include "../core/ui_config.h"
#include "notice_tracker.h"
#include "notices_hud.h"
#include "toast_feed.h"

using namespace PluginConstants;
using namespace DeckLayout;
using namespace DeckFormat;

namespace {
using NKind = NoticeTracker::Kind;

// The tab: where the notice comes from, in one word
const char* noticeTab(NKind kind) {
    switch (kind) {
    case NKind::WrongWay:
    case NKind::HazardAhead:
    case NKind::RiderOncoming:
    case NKind::BlueFlag:
    case NKind::LapperAhead:   return TR("ALERT");
    case NKind::AllTimePB:
    case NKind::FastestLap:
    case NKind::SessionPB:
    case NKind::SegmentAdded:
    case NKind::SegmentRemoved: return TR("LAP");
    case NKind::Overtime:
    case NKind::Finished:
    case NKind::LastLap:       return TR("RACE");
    case NKind::DefaultSetup:  return TR("SETUP");
    case NKind::TrackChanged:  return TR("MUSIC");
    case NKind::None:          break;
    }
    return "";
}

// The icon beside it, from the base icon set
const char* noticeIcon(NKind kind) {
    switch (kind) {
    case NKind::WrongWay:
    case NKind::HazardAhead:
    case NKind::RiderOncoming: return "triangle-exclamation";
    case NKind::BlueFlag:
    case NKind::LapperAhead:
    case NKind::LastLap:       return "flag";
    case NKind::Overtime:      return "hourglass-half";
    case NKind::AllTimePB:     return "trophy";
    case NKind::FastestLap:
    case NKind::SessionPB:     return "stopwatch";
    case NKind::SegmentAdded:
    case NKind::SegmentRemoved: return "location-dot";
    case NKind::Finished:      return "flag-checkered";
    case NKind::DefaultSetup:  return "wrench";
    case NKind::TrackChanged:  return "hud-nowplaying";
    case NKind::None:          break;
    }
    return "";
}
}  // namespace

DeckHud::NoticeUp DeckHud::noticeUp() const {
    const NoticeTracker::Notice& notice = NoticeTracker::getInstance().current();
    const ToastFeed& feed = ToastFeed::getInstance();
    HudManager& hm = HudManager::getInstance();
    // Hide-all: only that hotkey's own message comes through
    if (!hm.areHudsEnabled()) return feed.isThroughHideAll() ? NoticeUp::Toast : NoticeUp::None;
    // The carousel, held by a race notice (Notice focus), shows that notice only:
    // the toasts are the toast card's and the Notice deck's
    if (m_carousel) return notice.kind != NKind::None ? NoticeUp::Notice : NoticeUp::None;
    if (NoticeTracker::aboveToasts(notice.kind)) return NoticeUp::Notice;
    if (feed.showing()) return NoticeUp::Toast;
    if (notice.kind != NKind::None) return NoticeUp::Notice;
    // Its tab's positioning preview (the Decks tab's previewHud)
    return isPreviewing() ? NoticeUp::Preview : NoticeUp::None;
}

void DeckHud::clickNotice() {
    // A click on a toast opens the menu where it points, as on the toast card
    // (ownsClick: on screen, the topmost under the pointer)
    if (noticeUp() != NoticeUp::Toast || !ownsClick()) return;
    const InputManager& input = InputManager::getInstance();
    if (!input.isCursorEnabled() || !input.getLeftButton().isClicked()) return;
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    if (cursor.isValid && isPointInBounds(cursor.x, cursor.y)) ToastFeed::getInstance().click();
}

// The detail over the two detail lines: the first takes what fits the layout's
// note-detail text (its font, size and fit), broken at the last space; the
// rest goes to the second, which the drawer shrinks or cuts like any text.
void DeckHud::setNoticeDetail(const DeckLayout::Deck* deck, const char* detail) {
    if (!detail || !detail[0]) return;
    const Text* line = nullptr;
    if (deck) {
        for (int i = 0; i < deck->textCount; ++i) {
            if (deck->texts[i].present && deck->texts[i].field == NOTE_DETAIL) {
                line = &deck->texts[i];
                break;
            }
        }
    }
    const int len = static_cast<int>(std::strlen(detail));
    int cut = len;
    if (line && line->fit > 0.0f) {
        const int font = getFont(static_cast<FontCategory>(line->fontCategory));
        const int fits = AssetManager::getInstance().fitChars(font, detail, lx(line->fit), ly(line->size));
        if (fits < len) {
            cut = fits;
            while (cut > 0 && detail[cut] != ' ') --cut;
            if (cut == 0) cut = fits;   // one long word: cut it where it ends
        }
    }
    char buf[sizeof(m_rows.text[0])];
    snprintf(buf, sizeof(buf), "%.*s", cut, detail);
    setText(NOTE_DETAIL, buf);
    const char* rest = detail + cut;
    while (*rest == ' ') ++rest;
    if (*rest) setText(NOTE_DETAIL2, rest);
}

bool DeckHud::gatherNotice(const DeckLayout::Deck* deck) {
    const NoticeUp up = noticeUp();
    if (up == NoticeUp::None) return false;
    const NoticeTracker::Notice& notice = NoticeTracker::getInstance().current();
    const ToastFeed& feed = ToastFeed::getInstance();
    HudManager& hm = HudManager::getInstance();

    char note[sizeof(m_rows.text[0])] = "";
    const char* tab = "";
    const char* icon = "";
    const char* detail = "";
    unsigned long ink = 0;
    unsigned long iconInk = 0;
    if (up == NoticeUp::Notice) {
        snprintf(note, sizeof(note), "%s", notice.text);
        tab = noticeTab(notice.kind);
        icon = noticeIcon(notice.kind);
        detail = notice.detail;
        ink = iconInk = NoticesHud::messageColor(*this, notice.kind);
        if (NoticesHud::glows(notice.kind)) {
            m_rows.glow = NoticesHud::glowOpacity(notice.kind);
            m_rows.glowColor = ink;
        }
    } else if (up == NoticeUp::Toast && feed.source() == ToastFeed::Source::Achievement) {
        const AchievementManager::Toast& toast = feed.achievement();
        snprintf(note, sizeof(note), "%s", toast.title);
        tab = TR("ACHIEVEMENT");
        icon = toast.icon;
        detail = toast.detail;
        ink = iconInk = PodiumColors::tierColor(toast.tier, getColor(ColorSlot::PRIMARY));
    } else if (up == NoticeUp::Toast) {
        // A plugin message: the primary colour with an accent icon, or the
        // negative colour when it warns, as on the toast card
        const SystemMessages::Toast& message = feed.system();
        const bool warning = message.severity == SystemMessages::Severity::Warning;
        snprintf(note, sizeof(note), "%s", message.title);
        tab = TR("MESSAGE");
        icon = message.icon;
        detail = message.detail;
        ink = warning ? getColor(ColorSlot::NEGATIVE) : getColor(ColorSlot::PRIMARY);
        iconInk = warning ? ink : getColor(ColorSlot::ACCENT);
    } else {
        // The preview: what a message looks like, so the deck can be placed
        snprintf(note, sizeof(note), "%s", TR("PREVIEW"));
        tab = TR("MESSAGE");
        icon = "circle-exclamation";
        detail = TR("Notices and messages appear here");
        ink = getColor(ColorSlot::PRIMARY);
        iconInk = getColor(ColorSlot::ACCENT);
    }
    upper(note);
    setText(TITLE, tab);
    setText(detail[0] ? NOTE : NOTE_ALONE, note);
    setNoticeDetail(deck, detail);
    m_rows.ink[TITLE] = ink;
    m_rows.ink[NOTE] = ink;
    m_rows.ink[NOTE_ALONE] = ink;
    // The icon, when the set has it and icons are on (as on the toast card)
    if (icon[0] && UiConfig::getInstance().getTitleIcons()) {
        // Looked up by name only when the name or the theme changes
        const AssetManager& assets = AssetManager::getInstance();
        if (m_noticeIconGen != assets.themeGeneration() || std::strcmp(m_noticeIconName, icon) != 0) {
            m_noticeIconGen = assets.themeGeneration();
            snprintf(m_noticeIconName, sizeof(m_noticeIconName), "%s", icon);
            m_noticeIconSprite = assets.getIconSpriteIndex(icon);
        }
        m_rows.icon = m_noticeIconSprite;
        m_rows.iconInk = iconInk;
    }

    // What waits behind the one showing: both toast queues, the toast held
    // behind a notice, and the standing notice behind a toast
    int waiting = feed.waiting();
    if (up == NoticeUp::Notice && feed.showing()) ++waiting;
    if (up == NoticeUp::Toast && notice.kind != NKind::None && hm.areHudsEnabled()) ++waiting;
    if (waiting > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "+%d", std::min(waiting, 99));
        setText(STAMP, buf);
    }
    return true;
}
