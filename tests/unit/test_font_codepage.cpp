// ============================================================================
// tests/unit/test_font_codepage.cpp
// The plugin's font code page (core/font_codepage.h): CP1252 plus Ğ ğ İ ı Ş ş in
// the six bytes CP1252 leaves unused. Pins that the slots never collide with a
// CP1252 character, that text reaches them (a Turkish translation loads instead
// of falling back to English, a Turkish chat name draws instead of falling back
// to the login), and that Find folds them to their plain letter. That the
// shipped fonts draw them is test_font_metrics.cpp's.
// ============================================================================
#include "doctest.h"

#include "core/chat_text.h"
#include "core/font_codepage.h"
#include "core/i18n_catalog.h"
#include "hud/settings/settings_find.h"

#include <cctype>
#include <set>
#include <string>

TEST_CASE("font code page: the Turkish slots are bytes CP1252 leaves unused, and only they reach them") {
    std::set<unsigned char> slotBytes;
    for (const FontCodePage::ExtraSlot& s : FontCodePage::kExtraSlots) slotBytes.insert(s.byte);
    CHECK(slotBytes.size() == size_t(FontCodePage::kExtraSlotCount));
    // Every code point a CP1252 character could be lands on its own byte, never a
    // slot's -- so a slot never changes how existing text draws.
    for (uint32_t cp = 0; cp < 0x10000; ++cp) {
        const unsigned char b = ChatText::toCp1252(cp);
        if (!slotBytes.count(b)) continue;
        const FontCodePage::ExtraSlot* s = FontCodePage::slotForCodepoint(cp);
        REQUIRE_MESSAGE(s != nullptr, "code point " << cp << " lands on a Turkish slot");
        CHECK(s->byte == b);
    }
    for (const FontCodePage::ExtraSlot& s : FontCodePage::kExtraSlots) {
        CHECK(ChatText::toCp1252(s.codepoint) == s.byte);
        CHECK(FontCodePage::slotForByte(s.byte) == &s);
    }
}

TEST_CASE("font code page: a Turkish translation converts exactly instead of falling back") {
    std::string out;
    // "Değer İstatistik Şampiyon ışık" -- all six letters plus ç ö ü from Latin-1.
    REQUIRE(I18n::utf8ToCp1252Exact("De\xC4\x9F" "er \xC4\xB0statistik \xC5\x9E" "ampiyon "
                                    "\xC4\xB1\xC5\x9F\xC4\xB1k \xC3\xA7\xC3\xB6\xC3\xBC \xC4\x9E", out));
    CHECK(out == "De\x81" "er \x8D" "statistik \x90" "ampiyon \x8F\x9D\x8F" "k \xE7\xF6\xFC \x7F");
    // A Turkish chat name draws rather than falling back to the login.
    CHECK(ChatText::isFullyRenderable("\xC5\x9E\xC3\xBC" "kr\xC3\xBC Y\xC4\xB1" "ld\xC4\xB1z"));
}

TEST_CASE("font code page: Find folds the Turkish slots to their plain letter") {
    for (const FontCodePage::ExtraSlot& s : FontCodePage::kExtraSlots) {
        CHECK(SettingsFind::foldChar(s.byte) == static_cast<unsigned char>(std::tolower(s.plain)));
    }
}
