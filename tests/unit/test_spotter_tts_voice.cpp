// ============================================================================
// tests/unit/test_spotter_tts_voice.cpp
// Pins the in-game TTS voice picker's pure half (core/spotter_tts_voice.h).
//
// ESCAPING IS THE REASON THIS FILE EXISTS. Every spoken cue goes to SAPI with
// SPF_IS_XML, and cue phrases are USER-authored (pack inis are meant to be
// hand-edited). One unescaped "&" makes SAPI reject the utterance, so the
// spotter would go mute on exactly the packs people customise — a failure
// that shows up as silence, with nothing on screen to explain it. The
// subtitle would still be correct, which makes it worse.
//
// NOTE ON WHAT IS NOT HERE. There used to be a wrap() building
// `<voice required="Name=...">` around the text, and it never worked: the
// name that markup matches is the token's Attributes\Name, not the display
// name shown in the picker, and `required` drops the text SILENTLY on a
// mismatch. Selection is ISpObjectToken + SetVoice now, which has no string
// matching to get wrong and so nothing here to pin. The bug is worth knowing
// because its signature is deceptive: correct subtitle, S_OK from Speak(),
// no sound, and only for non-default voices.
//
// The rest pins the selection rules: stored-by-NAME resolution against the
// live list (a voice list differs per machine, so an index would mean a
// different voice on another PC), and cycling that treats "system default"
// as a real, reachable entry rather than an absence.
// ============================================================================
#include "doctest.h"

#include "core/spotter_tts_voice.h"

#include <string>
#include <vector>

using namespace SpotterTtsVoice;

TEST_CASE("escapeXml: every character that would break an utterance") {
    CHECK(escapeXml("Rider behind.") == "Rider behind.");
    // The one that matters in practice: a pack author writing "P1 & P2".
    CHECK(escapeXml("P1 & P2") == "P1 &amp; P2");
    CHECK(escapeXml("<voice>") == "&lt;voice&gt;");
    // Quotes are NOT escaped. SAPI does not resolve &apos; back into an
    // apostrophe -- it reads the entity as a word break, so escaping turned
    // "you're" into "you-reh" and "it's" into "it-ess" for every contraction in
    // the pack. Character data needs & and < gone; quotes need nothing.
    CHECK(escapeXml("say \"go\"") == "say \"go\"");
    CHECK(escapeXml("it's clear") == "it's clear");
    CHECK(escapeXml("You're the leader now.") == "You're the leader now.");
    // Order matters: an escaped ampersand must not be re-escaped into
    // "&amp;amp;" — one pass, left to right.
    CHECK(escapeXml("a & b < c") == "a &amp; b &lt; c");
    CHECK(escapeXml("") == "");
}

TEST_CASE("speakable: escaped, never wrapped, whichever voice is speaking") {
    CHECK(speakable("Rider behind.") == "Rider behind.");
    CHECK(speakable("P1 & P2") == "P1 &amp; P2");
    // Unconditional: the utterance carries no voice markup at all now, so
    // there is no second mode in which a pack phrase could escape differently.
    // A `<` a pack author typed is text, not the start of an element.
    CHECK(speakable("<voice>") == "&lt;voice&gt;");
    CHECK(speakable("") == "");
}

TEST_CASE("resolve: stored by name, degrading to the default when absent") {
    const std::vector<std::string> installed = { "Microsoft David Desktop",
                                                 "Microsoft Zira Desktop" };
    CHECK(resolve(installed, "Microsoft Zira Desktop") ==
          "Microsoft Zira Desktop");
    // Not installed on THIS machine: the system default, and the caller
    // keeps the stored name (the setting is not rewritten) so reinstalling
    // the voice restores the choice.
    CHECK(resolve(installed, "Microsoft Hazel Desktop") == "");
    // Exact matching: SAPI's own name lookup is exact, so a near-miss is a
    // different voice rather than this one.
    CHECK(resolve(installed, "microsoft zira desktop") == "");
    CHECK(resolve(installed, "") == "");
    CHECK(resolve({}, "Microsoft David Desktop") == "");
}

TEST_CASE("cycle: the system default is a reachable entry, both directions") {
    const std::vector<std::string> installed = { "David", "Zira" };

    // Forward from the default walks the list, then wraps back to default.
    CHECK(cycle(installed, "", true) == "David");
    CHECK(cycle(installed, "David", true) == "Zira");
    CHECK(cycle(installed, "Zira", true) == "");

    // Backward is the mirror.
    CHECK(cycle(installed, "", false) == "Zira");
    CHECK(cycle(installed, "Zira", false) == "David");
    CHECK(cycle(installed, "David", false) == "");

    // A stored voice that is no longer installed cycles from the default
    // rather than getting stuck or vanishing.
    CHECK(cycle(installed, "Hazel", true) == "David");

    // No voices at all (every Wine prefix): the default is the only stop,
    // in both directions — never an out-of-range pick.
    CHECK(cycle({}, "", true) == "");
    CHECK(cycle({}, "", false) == "");
    CHECK(cycle({}, "David", true) == "");
}

// Windows keeps voices in TWO registry hives and SAPI only reads one of them:
// `Speech\Voices\Tokens` (the classic "Desktop" voices) and
// `Speech_OneCore\Voices\Tokens` (the newer engines, and every voice a player
// adds through Settings > Time & Language > Speech — those install THERE and
// nowhere else). Enumerating only the first is why a voice installed in
// Windows never showed up in the picker.

// The settings row is ten characters wide and every Windows voice is named
// "Microsoft <who> - English (<place>)", so every one of them rendered as
// "Microso..." — the same string for all of them, telling a player nothing
// about what they had just selected.
TEST_CASE("displayName: keeps the part that differs") {
    CHECK(displayName("Microsoft David Desktop - English (United States)") ==
          "David Desktop - English");
    CHECK(displayName("Microsoft Hazel - English (United Kingdom)") ==
          "Hazel - English");
    // A third-party engine's voice, which has neither the locale suffix nor
    // anything else to trim.
    CHECK(displayName("Microsoft Ryan") == "Ryan");
    // Not a Microsoft voice: left alone but for the locale.
    CHECK(displayName("Acme Narrator - English (Australia)") ==
          "Acme Narrator - English");
    CHECK(displayName("Custom") == "Custom");
    // Never empty, whatever it is handed — an empty row would read as "System
    // default", which is a different setting.
    CHECK(displayName("Microsoft ") == "Microsoft ");
    CHECK(displayName("Microsoft (United States)") == "Microsoft (United States)");
    CHECK(displayName("") == "");
}

// The purge-drain verdict, and the crash it pins. Three shipped dumps carry
// the SAME faulting instruction — a string scan in espeak-ng.dll+0x74ed8
// dereferencing a wild pointer — from users cycling TTS voices in the
// settings menu (v1.27-era, then twice in v1.29.1.381 despite the SEH guards
// and a one-slice drain). The engine faults when the plugin calls back into
// it — SetVoice, Release, or a new utterance — while a purged utterance is
// still unwinding: espeak-ng keeps GLOBAL synth state, so touching it
// mid-teardown is a use-after-free inside the engine. The one-slice drain
// IGNORED WaitUntilDone's return, so past 250ms the worker proceeded into
// exactly that state. The rule drainVerdict holds: nothing touches the voice
// object again until a wait CONFIRMS idle (S_OK); a drain that runs out of
// slices, or whose wait fails, wedges — and the worker abandons the object
// rather than making one more call into it.
TEST_CASE("drainVerdict: only a confirmed S_OK ends the drain as idle") {
    constexpr long kSOk = 0, kSFalse = 1, kEFail = -2147467259; // 0x80004005
    // Confirmed idle, whenever it comes — first slice or last.
    CHECK(drainVerdict(kSOk, 1, 10) == DrainVerdict::Idle);
    CHECK(drainVerdict(kSOk, 10, 10) == DrainVerdict::Idle);
    // Timeout below the cap keeps draining; it never proceeds early.
    CHECK(drainVerdict(kSFalse, 1, 10) == DrainVerdict::Again);
    CHECK(drainVerdict(kSFalse, 9, 10) == DrainVerdict::Again);
    // The cap is a wedge, not a shrug: slices exhausted without S_OK means
    // hands off the object, never "proceed anyway" — proceeding anyway IS
    // the crash.
    CHECK(drainVerdict(kSFalse, 10, 10) == DrainVerdict::Wedged);
    CHECK(drainVerdict(kSFalse, 11, 10) == DrainVerdict::Wedged);
    // A FAILING wait is no confirmation of anything: don't trust the object.
    CHECK(drainVerdict(kEFail, 1, 10) == DrainVerdict::Wedged);
    // The shutdown cap is tighter (boundedness is the point of the async
    // speak loop); the verdict shape is identical.
    CHECK(drainVerdict(kSFalse, 1, 2) == DrainVerdict::Again);
    CHECK(drainVerdict(kSFalse, 2, 2) == DrainVerdict::Wedged);
}
