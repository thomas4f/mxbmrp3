// ============================================================================
// core/spotter_tts_voice.h
// Picking WHICH Windows TTS voice the spotter speaks with, without leaving
// the game. Pure (strings in, strings out) so the unit suite drives every
// edge (test_spotter_tts_voice.cpp); SpotterManager enumerates the installed
// voices and its worker speaks the wrapped text.
//
// SELECTION IS ISpVoice::SetVoice, NOT `<voice required="Name=...">` MARKUP.
// It was the markup, and it did not work: every non-default voice showed its
// subtitle and played nothing at all.
//
// The reason is a real trap in SAPI's registry layout. A voice token key's
// DEFAULT value is its DISPLAY name — "Microsoft David Desktop - English
// (United States)" — while the `Name` that `required="Name=..."` matches is
// the token's `Attributes\Name` subvalue, "Microsoft David Desktop". They are
// different strings for essentially every shipped voice, so the match could
// never succeed. And `required` fails SILENTLY: SAPI drops the enclosed text
// rather than erroring, so Speak() returned S_OK, nothing was heard, and the
// subtitle (which never went through SAPI) appeared as normal. The system
// default kept working the whole time because that path emits no voice tag.
//
// So the identifier is now the voice's DISPLAY NAME, matched against a live
// enumeration rather than resolved through the registry at all. The settings
// list and the selection are the same strings, produced by the same
// ISpObjectTokenCategory walk on the worker (see
// SpotterManager::enumerateTtsVoicesViaSapi); the worker finds the token whose
// display name matches and calls SetVoice with it.
//
// A stored name that no longer resolves — a voice uninstalled, or a machine
// with no SAPI at all — falls back to the system default WITHOUT rewriting the
// setting, so plugging the voice back in restores the choice. Same rule as the
// pack name.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace SpotterTtsVoice {

// XML entity-escape for CHARACTER DATA, which is the only place this text ever
// lands: `&` and `<` must go, `>` follows them by convention, and that is the
// whole list XML requires.
//
// QUOTES ARE DELIBERATELY NOT ESCAPED. An earlier revision escaped ' and " as
// well, on the theory that one helper could also serve attribute values -- but
// nothing builds an attribute any more (voice selection is SetVoice, see the
// header), and SAPI does not resolve &apos; back into an apostrophe. It read
// the entity as a word break instead, so "you're" came out "you-reh" and
// "it's" came out "it-ess". Escaping a character that needs no escaping is
// never free: it either round-trips or it corrupts, and here it corrupted every
// contraction in the pack. If an attribute value ever needs building again,
// give it its own helper rather than widening this one.
inline std::string escapeXml(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            default:   out += c;        break;
        }
    }
    return out;
}

// The string to hand ISpVoice::Speak. Escaped, never wrapped: the WHICH-voice
// question is answered by SetVoice before the call (see the header), and this
// is only about surviving SPF_IS_XML. Always escaped, never conditionally, so
// a stray "&" in a pack phrase fails the same way every time rather than only
// in some configurations.
inline std::string speakable(const std::string& text) {
    return escapeXml(text);
}

// The voice actually usable right now: the stored name when it is installed,
// else "" (the system default). Matching is exact — SAPI's own name lookup is,
// so a near-miss is a different voice, not this one.
inline std::string resolve(const std::vector<std::string>& available,
                           const std::string& storedName) {
    if (storedName.empty()) return std::string();
    for (const std::string& v : available) {
        if (v == storedName) return storedName;
    }
    return std::string();
}

// What the settings row SHOWS for a voice. Every Windows voice is named
// "Microsoft <who> - English (United Kingdom)", and the row is ten characters
// wide — so every one of them rendered as "Microso...", which is the same
// string for all of them and tells a player nothing about what they picked.
//
// The prefix is dropped and the locale parenthetical with it, leaving the part
// that differs: "David Desktop - English", "Ryan". Display only — the STORED
// value stays the full name, because that is what SAPI is asked to match (see
// the stored-by-name note above), and two voices could otherwise shorten to
// the same thing.
inline std::string displayName(const std::string& full) {
    std::string out = full;
    const std::string prefix = "Microsoft ";
    if (out.rfind(prefix, 0) == 0) out = out.substr(prefix.size());
    const size_t paren = out.find(" (");
    if (paren != std::string::npos) out = out.substr(0, paren);
    // Trimming must never leave LESS than a name. A voice called nothing but
    // its locale would come back empty or as "(United States)", and an empty
    // row reads as "System default" — a different setting entirely. Anything
    // that shortens to a non-name keeps the full string instead.
    if (out.empty() || out[0] == '(') return full;
    return out;
}

// After purging an utterance, the worker drains in bounded slices until the
// engine CONFIRMS idle -- WaitUntilDone returning S_OK, the only "engine is
// idle" SAPI offers. This is the per-slice verdict. The constraint it holds:
// no further call may be made into a voice object that has not confirmed idle
// (SetVoice, Release and CoUninitialize's teardown pump all walk the engine's
// live state), so a drain that runs out of slices or whose wait FAILS yields
// Wedged, which the worker maps to "abandon the object, hands off" -- never
// "proceed anyway". Pure so the unit suite pins every edge; the bug it
// prevents is in test_spotter_tts_voice.cpp's drain case.
enum class DrainVerdict {
    Idle,    // engine confirmed idle -- safe to touch again
    Again,   // not idle yet -- wait another slice
    Wedged,  // never confirmed idle, or the wait failed -- abandon the object
};

// waitHr is the HRESULT of the slice's WaitUntilDone as a long (S_OK = 0,
// S_FALSE/timeout = 1, failures < 0), slicesDone counts this slice as done.
inline DrainVerdict drainVerdict(long waitHr, int slicesDone, int sliceCap) {
    if (waitHr == 0) return DrainVerdict::Idle;
    if (waitHr < 0) return DrainVerdict::Wedged;   // a failing wait is no confirmation
    return slicesDone >= sliceCap ? DrainVerdict::Wedged : DrainVerdict::Again;
}

// Step the selection one place through [system default] + available voices,
// wrapping at both ends. The empty string is a real entry (position 0), not
// an absence — "System default" is a choice a player can cycle back to.
// A stored name that is no longer installed cycles from the default rather
// than getting lost, and an empty list always yields the default.
inline std::string cycle(const std::vector<std::string>& available,
                         const std::string& current, bool forward) {
    if (available.empty()) return std::string();
    // Index 0 = default; 1..N = available[0..N-1].
    size_t idx = 0;
    for (size_t i = 0; i < available.size(); ++i) {
        if (available[i] == current) { idx = i + 1; break; }
    }
    const size_t count = available.size() + 1;
    idx = forward ? (idx + 1) % count : (idx + count - 1) % count;
    return idx == 0 ? std::string() : available[idx - 1];
}

}  // namespace SpotterTtsVoice
