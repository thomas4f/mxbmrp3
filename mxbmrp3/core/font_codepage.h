// ============================================================================
// core/font_codepage.h
// The plugin's font code page: CP1252, plus the six Turkish letters CP1252 lacks
// placed in the six byte values CP1252 leaves unused.
//
// WHY NOT A REAL CP1254 SET. The game's .fnt is a fixed table of 256 glyphs
// indexed by byte, and the game is handed the font list once, at DrawInit. A
// per-language font set would need a game restart to switch language and would
// lose CP1252's Ð Ý Þ ð ý þ (Czech ý included) in rider names while Turkish is
// on. CP1252 leaves exactly six byte values undefined (0x7F DEL and 0x81 0x8D
// 0x8F 0x90 0x9D), and Turkish needs exactly six letters it lacks (ç ö ü â are
// Latin-1), so every font carries both at once and nothing is lost.
//
// ONE TABLE, THREE READERS: the .fnt generator (tools/fontgen) draws these
// glyphs into the slots, ChatText::toCp1252 maps text onto them, and the
// settings Find folds them to their plain letter. Ğ takes 0x7F because it never
// starts a Turkish word, so it is the letter least often drawn. Pinned by
// test_font_codepage.cpp (mapping, Find) and test_font_metrics.cpp (every
// shipped font inks a slot wherever it inks the slot's plain letter).
// ============================================================================
#pragma once

#include <cstdint>

namespace FontCodePage {

struct ExtraSlot {
    unsigned char byte;    // the byte the text carries and the .fnt indexes
    uint32_t codepoint;    // the Unicode letter drawn there
    char plain;            // the unaccented letter: Find folds to it, and the
                           // generator draws it when a font has neither the
                           // letter nor a mark to build it from
};

inline constexpr ExtraSlot kExtraSlots[] = {
    { 0x7F, 0x011E, 'G' },   // Ğ
    { 0x81, 0x011F, 'g' },   // ğ
    { 0x8D, 0x0130, 'I' },   // İ
    { 0x8F, 0x0131, 'i' },   // ı
    { 0x90, 0x015E, 'S' },   // Ş
    { 0x9D, 0x015F, 's' },   // ş
};
inline constexpr int kExtraSlotCount = static_cast<int>(sizeof(kExtraSlots) / sizeof(kExtraSlots[0]));

// The slot drawing a code point, or nullptr.
constexpr const ExtraSlot* slotForCodepoint(uint32_t cp) {
    for (const ExtraSlot& s : kExtraSlots) {
        if (s.codepoint == cp) return &s;
    }
    return nullptr;
}

// The slot at a byte, or nullptr.
constexpr const ExtraSlot* slotForByte(unsigned char b) {
    for (const ExtraSlot& s : kExtraSlots) {
        if (s.byte == b) return &s;
    }
    return nullptr;
}

}  // namespace FontCodePage
