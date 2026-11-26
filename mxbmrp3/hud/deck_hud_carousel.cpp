// ============================================================================
// hud/deck_hud_carousel.cpp
// The carousel deck (deck_hud.h, THE CAROUSEL): which deck shows, the
// steps between them and what a step draws. A step is the Dash's (PageFlip):
// the deck stepped away from fades out as it was on screen, the deck's name
// holds in its place, then crossfades into its readouts. The deck's art is
// the new deck's from the first frame: the decks it shows share a size, so
// only the readouts move.
// ============================================================================
#include "deck_hud.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../core/asset_manager.h"
#include "../core/input_manager.h"
#include "../core/media_manager.h"
#include "../core/motion.h"
#include "../core/plugin_constants.h"
#include "../core/i18n.h"
#include "notice_tracker.h"
#include "now_playing_card.h"

using namespace DeckLayout;

namespace {
// The dots marker, in shares of the title's size: each dot, the gap between
// two, and the space before the first (the Decks workbench's)
constexpr float DOT = 0.4f;
constexpr float DOT_GAP = 0.25f;
constexpr float DOT_LEAD = 0.55f;
// The name may take this much of the deck's width before it shrinks
constexpr float NAME_ROOM = 0.9f;

int countOn(uint32_t on) {
    int n = 0;
    for (int i = 0; i < DeckHud::CAROUSEL_SIZE; ++i) n += (on >> i) & 1u;
    return n;
}
}  // namespace

void DeckHud::resetCarousel() {
    m_carDecks = CAROUSEL_DEFAULT;
    m_carAuto = 0;
    m_carMarker = Marker::Dots;
    m_carOptions = CAR_NAMES | CAR_NOTICES;
    m_carIndex = 0;
    m_carHold = false;
    m_carStepUs = 0;
    m_carSeenNotice = NoticeTracker::getInstance().revision();
    m_kind = carouselMember();
    m_flip.stopFade();
    m_flip.clearName();
}

uint32_t DeckHud::carouselOn() const {
    const uint32_t picked = (m_carDecks & CAROUSEL_ALL) ? (m_carDecks & CAROUSEL_ALL) : CAROUSEL_ALL;   // a hand-edited 0 shows all
    const uint32_t on = m_carMediaLive ? picked : picked & ~CAROUSEL_MEDIA;
    return on ? on : picked;   // Now Playing alone stays, with its "No player"
}

void DeckHud::carouselStep(int direction, bool named) {
    if (!m_carousel) return;
    const int64_t now = DigitRoll::nowUs();
    // A step while a notice holds it is the rider taking it back: their deck
    // returns, and that notice is done with
    if (m_carHold) {
        endCarouselHold(now);
        return;
    }
    m_carStepUs = now;
    const uint32_t on = carouselOn();
    int next = m_carIndex;
    for (int i = 0; i < CAROUSEL_SIZE; ++i) {
        next = ((next + direction) % CAROUSEL_SIZE + CAROUSEL_SIZE) % CAROUSEL_SIZE;
        if (on & (1u << next)) break;
    }
    if (next == m_carIndex) return;
    m_carIndex = next;
    carouselShow(carouselMember(), named);
}

void DeckHud::endCarouselHold(int64_t nowUs) {
    m_carHold = false;
    m_carStepUs += nowUs - m_carHoldUs;   // the Auto clock waited
    carouselShow(carouselMember(), false);
}

void DeckHud::carouselShow(Kind next, bool named) {
    if (next == m_kind) return;
    // What is on screen of the deck leaving -- its readouts and its name, at
    // the alpha they have now -- fades out from there
    m_fromQuads.clear();
    m_fromStrings.clear();
    m_fromSkipShadow.clear();
    for (int i = 0; i < m_contentQuadCount; ++i) {
        for (size_t q = m_contentQuads[i].from; q < m_contentQuads[i].to && q < m_quads.size(); ++q)
            m_fromQuads.push_back(m_quads[q]);
    }
    const auto keepString = [this](size_t i) {
        if (i >= m_strings.size()) return;
        m_fromStrings.push_back(m_strings[i]);
        m_fromSkipShadow.push_back(i < m_stringSkipShadow.size() && m_stringSkipShadow[i]);
    };
    for (int i = 0; i < m_contentStringCount; ++i) {
        for (size_t k = m_contentStrings[i].from; k < m_contentStrings[i].to; ++k) keepString(k);
    }
    if (m_nameString >= 0) keepString(static_cast<size_t>(m_nameString));
    m_flip.go(static_cast<int>(m_kind), static_cast<int>(next));
    m_kind = next;
    // As a deck shown again: nothing that happened meanwhile is news
    forgetUnshown();
    // This frame's update has already ranked and stepped for the deck
    // leaving: the new deck's place and Rival's gaps are read now, so the
    // first gather of the step shows this frame's, not what was left from
    // its last turn
    if (ranks()) refreshRank();
    if (m_kind == Kind::Rival) stepRival(DigitRoll::nowUs());
    m_stamp = Stamp::None;
    m_stampUntilUs = 0;
    if (named && (m_carOptions & CAR_NAMES)) m_flip.showName();
    setDataDirty();
}

void DeckHud::followCarousel() {
    const int64_t now = DigitRoll::nowUs();
    // The Auto clock starts when it is first shown (0), and again if Motion's
    // clock went back past it
    if (m_carStepUs <= 0 || m_carStepUs > now) m_carStepUs = now;
    // Now Playing joins the turn while a player plays or pauses: the snapshot
    // read only when the worker's revision or the switch moved
    const MediaManager& media = MediaManager::getInstance();
    if (media.revision() != m_carMediaRev || media.isEnabled() != m_carMediaOn) {
        m_carMediaRev = media.revision();
        m_carMediaOn = media.isEnabled();
        MediaManager::Snapshot np;
        media.getSnapshot(np);
        const bool live = NowPlaying::hasPlayer(media, np);
        if (live != m_carMediaLive) {
            m_carMediaLive = live;
            setDataDirty();   // the marker counts the turn
        }
    }
    // The deck showing switched off in the settings, or Now Playing with
    // nothing playing: on to the next one on
    const uint32_t on = carouselOn();
    if (!(on & (1u << m_carIndex))) {
        for (int i = 1; i <= CAROUSEL_SIZE; ++i) {
            const int k = (m_carIndex + i) % CAROUSEL_SIZE;
            if (on & (1u << k)) {
                m_carIndex = k;
                break;
            }
        }
        if (!m_carHold) carouselShow(carouselMember(), false);
    }

    // A left click on the title steps on, as Deck + (the rider's: named)
    const InputManager& input = InputManager::getInstance();
    if (m_carTitleR > m_carTitleL && input.isCursorEnabled() && input.getLeftButton().isClicked() && ownsClick()) {
        CursorPosition cursor = input.getCursorPosition();
        mapCursorToHudSpace(cursor.x, cursor.y);
        // The title's run is the deck's own space: the cursor, less where the deck sits
        const float cx = cursor.x - getOffsetX(), cy = cursor.y - getOffsetY();
        if (cursor.isValid && cx >= lx(m_carTitleL) && cx <= lx(m_carTitleR) &&
            cy >= ly(m_carTitleT) && cy <= ly(m_carTitleB))
            carouselStep(+1, true);
    }

    // NOTICE FOCUS: a new race notice takes it over (unnamed), for
    // CAROUSEL_NOTICE_US from the latest one or until none is up
    const NoticeTracker& notices = NoticeTracker::getInstance();
    if (notices.revision() != m_carSeenNotice) {
        m_carSeenNotice = notices.revision();
        if ((m_carOptions & CAR_NOTICES) && notices.current().kind != NoticeTracker::Kind::None) {
            if (!m_carHold) {
                m_carHold = true;
                carouselShow(Kind::Notice, false);
            }
            m_carHoldUs = now;
        }
    }
    if (m_carHold && (now - m_carHoldUs >= CAROUSEL_NOTICE_US || noticeUp() == NoticeUp::None)) endCarouselHold(now);

    // AUTO: on to the next deck this often, unnamed (only the rider's step is)
    const int autoS = AUTO_STEPS_S[std::clamp(m_carAuto, 0, AUTO_STEP_COUNT - 1)];
    if (autoS > 0 && !m_carHold && now - m_carStepUs >= static_cast<int64_t>(autoS) * 1000000) carouselStep(+1, false);

    // The flip draws every frame while it runs: the gathered readouts again
    if (m_flip.tick()) setLayoutDirty();
}

void DeckHud::carouselTitle() {
    snprintf(m_carName, sizeof(m_carName), "%s", m_rows.text[TITLE]);
    if (m_carMarker != Marker::Count || m_carHold || !m_rows.text[TITLE][0]) return;
    const uint32_t on = carouselOn();
    const int n = countOn(on);
    if (n < 2) return;
    const int at = countOn(on & ((1u << m_carIndex) - 1u)) + 1;
    char* title = m_rows.text[TITLE];
    const size_t len = std::strlen(title);
    snprintf(title + len, sizeof(m_rows.text[TITLE]) - len, " %d/%d", at, n);
}

void DeckHud::markContent(size_t quadFrom, size_t stringFrom) {
    const auto mark = [](Range* ranges, int& count, size_t from, size_t to) {
        if (to <= from) return;
        if (count > 0 && ranges[count - 1].to == from) {
            ranges[count - 1].to = static_cast<uint16_t>(to);
        } else if (count < MAX_RANGES) {
            ranges[count++] = Range{ static_cast<uint16_t>(from), static_cast<uint16_t>(to) };
        } else {
            ranges[count - 1].to = static_cast<uint16_t>(to);   // full: the last runs on (never on a shipped deck)
        }
    };
    mark(m_contentQuads, m_contentQuadCount, quadFrom, m_quads.size());
    mark(m_contentStrings, m_contentStringCount, stringFrom, m_strings.size());
}

void DeckHud::finishCarousel(const Deck& deck) {
    m_nameString = -1;
    const float in = m_flip.inAlpha();
    const float share = m_flip.nameShare();
    // The readouts at what the name leaves them; laid out even under a full
    // name, so the key's values stay current
    const float content = in * (1.0f - share);
    if (content < 1.0f) {
        for (int i = 0; i < m_contentQuadCount; ++i) {
            for (size_t q = m_contentQuads[i].from; q < m_contentQuads[i].to && q < m_quads.size(); ++q)
                m_quads[q].m_ulColor = Motion::scaleAlpha(m_quads[q].m_ulColor, content);
        }
        for (int i = 0; i < m_contentStringCount; ++i) {
            for (size_t k = m_contentStrings[i].from; k < m_contentStrings[i].to && k < m_strings.size(); ++k)
                m_strings[k].m_ulColor = Motion::scaleAlpha(m_strings[k].m_ulColor, content);
        }
    }
    // The name, in the title face at the deck's largest size, centred on the
    // deck's first box (its body), shrunk to fit it
    if (in * share > 0.0f && m_carName[0]) {
        float l = 0.0f, t = 0.0f, r = deck.width, b = deck.height, size = 0.0f;
        const Box& body = deck.boxes[0];
        if (deck.boxCount > 0 && body.present && !body.row) {
            l = std::min(body.quad[0], body.quad[2]);
            r = std::max(body.quad[4], body.quad[6]);
            t = std::min(body.quad[1], body.quad[7]);
            b = std::max(body.quad[3], body.quad[5]);
        }
        for (int i = 0; i < deck.textCount; ++i) {
            if (deck.texts[i].present && !deck.texts[i].row) size = std::max(size, deck.texts[i].size);
        }
        const int font = getFont(FontCategory::TITLE);
        float fs = ly(size);
        const float room = lx((r - l) * NAME_ROOM);
        const AssetManager& assets = AssetManager::getInstance();
        while (fs > ly(size) * FIT_MIN_SHARE && assets.textWidth(font, m_carName, fs) > room) fs *= 0.9f;
        m_nameString = static_cast<int>(m_strings.size());
        addString(m_carName, lx((l + r) * 0.5f), inkCenteredY(ly(t), ly(b - t), fs), PluginConstants::Justify::CENTER,
                  font, Motion::scaleAlpha(getColor(ColorSlot::PRIMARY), in * share), fs);
    }
    // The deck stepped away from, as it was on screen, fading out
    if (!m_flip.fading()) return;
    const float out = 1.0f - m_flip.progress();
    if (out <= 0.0f) return;
    for (SPluginQuad_t q : m_fromQuads) {
        q.m_ulColor = Motion::scaleAlpha(q.m_ulColor, out);
        m_quads.push_back(q);
    }
    for (size_t i = 0; i < m_fromStrings.size(); ++i) {
        SPluginString_t s = m_fromStrings[i];
        s.m_ulColor = Motion::scaleAlpha(s.m_ulColor, out);
        m_strings.push_back(s);
        m_stringSkipShadow.push_back(m_fromSkipShadow[i]);
    }
}

float DeckHud::markerWidth(const Text& t) const {
    if (!m_carousel || m_carHold || m_carMarker != Marker::Dots) return 0.0f;
    const int n = countOn(carouselOn());
    if (n < 2) return 0.0f;
    return t.size * (DOT_LEAD + DOT * static_cast<float>(n) + DOT_GAP * static_cast<float>(n - 1));
}

float DeckHud::widestName(int font, float fs) const {
    const AssetManager& assets = AssetManager::getInstance();
    const uint32_t members = carouselOn();
    float w = 0.0f;
    for (int i = 0; i < CAROUSEL_SIZE; ++i) {
        if (members & (1u << i)) w = std::max(w, assets.textWidth(font, title(CAROUSEL_KINDS[i]), fs));
    }
    return w / lx(1.0f);
}

void DeckHud::drawMarker(const Text& t, float x, unsigned long on) {
    const AssetManager& assets = AssetManager::getInstance();
    if (m_dotSpriteGen != assets.themeGeneration()) {
        m_dotSpriteGen = assets.themeGeneration();
        m_dotSprite = assets.getIconSpriteIndex("circle");
    }
    const unsigned long off = getColor(ColorSlot::MUTED);
    const float d = t.size * DOT, top = t.y + (t.h - d) * 0.5f;
    const int sprite = m_dotSprite > 0 ? m_dotSprite : PluginConstants::SpriteIndex::SOLID_COLOR;
    const uint32_t members = carouselOn();
    float left = x + t.size * DOT_LEAD;
    for (int i = 0; i < CAROUSEL_SIZE; ++i) {
        if (!(members & (1u << i))) continue;
        addRectPx(left, top, left + d, top + d, i == m_carIndex ? on : off, sprite);
        left += d + t.size * DOT_GAP;
    }
}
