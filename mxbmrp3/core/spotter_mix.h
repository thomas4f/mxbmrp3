// ============================================================================
// core/spotter_mix.h
// The spotter's wav chunk mixer: stitches a dynamic callout ("Fastest lap,
// rider four seventy six, one forty eight point two.") from a pack's chunk
// wavs at playback time, so packs stay ~1000 clips instead of one clip per
// possible sentence. Pure — parsing, token resolution and PCM assembly only;
// SpotterManager's worker does the file reads and hands the built buffer to
// PlaySound(SND_MEMORY).
//
// CHUNK NAMING is a frozen pack convention (documented in spotter_cue_pack.h
// with the rest of the format):
//   num_<N>.wav   N in 0..999, spoken racing-style ("four seventy six")
//   rider.wav     the word "rider"
//   point.wav     the word "point"
//   hundred.wav / oh.wav   the two words 3-digit decomposition needs
//   seconds.wav / second.wav   the {penalty_seconds} suffix words (penalty amounts)
//   laps.wav / lap.wav   the {overtime_laps} suffix words (overtime_started)
// {event_rider} resolves to rider.wav + num_<raceNum>.wav; {event_time} to
// num_<composed>.wav + point.wav + num_<tenths>.wav, where composed is the
// same minutes*100+seconds value SpotterPhrase::numberWords reads racing-
// style ("one forty eight" for 1:48). One vocabulary serves both. {penalty_seconds}
// resolves to num_<N>.wav + seconds.wav (second.wav when N is 1) and is the
// one OPTIONAL token — absent seconds omit it instead of aborting the mix.
//
// LIGHTWEIGHT PACKS: a pack need not carry all 900 three-digit clips. When
// the worker can't load num_<N>.wav for N >= 100, decomposeNumFile() splits
// it at the hundreds boundary along the same racing-style reading —
// num_142 -> num_1 + num_42 ("one | forty two"), num_500 -> num_5 + hundred,
// num_305 -> num_3 + oh + num_5 — so ~104 chunks (num_0..99, hundred, oh,
// rider, point) cover every number. This is what makes HUMAN-recorded packs
// practical: nobody records a thousand numbers. A full pack's whole-number
// clips always win when present (zero seams inside the number).
//
// TRUST BOUNDARY: chunk wavs are USER-SUPPLIED files from shared packs, so
// parseWav is a hardened reader in the mold of hud_sw_renderer's asset
// parsers — every offset bounds-checked, only the exact format the packs
// bake (PCM16 mono) accepted, anything else rejected whole. The unit-asan
// flavor of test_spotter_mix.cpp is the gate with teeth here.
// ============================================================================
#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "spotter_phrase.h"
#include "spotter_vars.h"

namespace SpotterMix {

// A canonical PCM16 mono RIFF header — what assemble() writes and what
// parseWav() requires at minimum. Named so no site spells the literal 44.
constexpr size_t kHeaderSize = 44;

// A parsed chunk: raw PCM16 samples + the rate they play at.
struct Pcm {
    std::vector<int16_t> samples;
    int sampleRate = 0;
    bool valid() const { return sampleRate > 0 && !samples.empty(); }
};

// Parse a PCM16 mono RIFF/WAVE file. Returns an invalid Pcm on ANY deviation
// — wrong magic, compressed/stereo/other-width formats, truncated chunks,
// sizes pointing past the end. Deliberately no partial acceptance.
inline Pcm parseWav(const uint8_t* data, size_t size) {
    Pcm out;
    auto u16 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[off]) |
               (static_cast<uint32_t>(data[off + 1]) << 8);
    };
    auto u32 = [&](size_t off) -> uint32_t {
        return u16(off) | (u16(off + 2) << 16);
    };

    if (!data || size < kHeaderSize) return out;
    if (std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0) {
        return out;
    }

    int sampleRate = 0;
    bool fmtOk = false;
    size_t pos = 12;
    // Chunk walk. Sizes are attacker-controlled: every read is bounds-checked
    // against the real buffer, never against the RIFF header's own size.
    while (pos + 8 <= size) {
        const uint32_t chunkSize = u32(pos + 4);
        const size_t body = pos + 8;
        if (chunkSize > size - body) return Pcm{};  // lies about its size

        if (std::memcmp(data + pos, "fmt ", 4) == 0) {
            if (chunkSize < 16) return Pcm{};
            const uint32_t format = u16(body);
            const uint32_t channels = u16(body + 2);
            const uint32_t rate = u32(body + 4);
            const uint32_t bits = u16(body + 14);
            if (format != 1 || channels != 1 || bits != 16 || rate == 0 ||
                rate > 192000) {
                return Pcm{};
            }
            sampleRate = static_cast<int>(rate);
            fmtOk = true;
        } else if (std::memcmp(data + pos, "data", 4) == 0) {
            if (!fmtOk) return Pcm{};  // data before fmt: reject
            const size_t sampleCount = chunkSize / 2;
            out.samples.resize(sampleCount);
            for (size_t i = 0; i < sampleCount; ++i) {
                out.samples[i] = static_cast<int16_t>(u16(body + i * 2));
            }
            out.sampleRate = sampleRate;
            return out;
        }
        // Chunks are word-aligned; a padded odd size must advance past its pad.
        pos = body + chunkSize + (chunkSize & 1);
    }
    return Pcm{};
}

// Scale a RIFF's samples in place for the [Spotter] volume (0..100), so the
// wav backends honour the same slider SAPI does. Without this the control
// moves nothing on the DEFAULT audio path — packs ship as fixed-level wavs
// and PlaySound has no per-sound volume, so the only thing the slider
// touches is TTS, which most players never hear.
//
// Linear on amplitude, not perceptual: 50% reads as "half as loud" closely
// enough for one-second callouts, and a curve here would fight the level
// the pack was baked at. 100 is a no-op memcpy-free fast path, and the
// clamp is a formality (scaling down cannot overflow) kept for the day
// somebody allows >100.
inline void applyGain(std::vector<uint8_t>& riff, int volumePct) {
    if (volumePct >= 100 || riff.size() <= kHeaderSize) return;
    if (volumePct < 0) volumePct = 0;
    for (size_t i = kHeaderSize; i + 1 < riff.size(); i += 2) {
        const int16_t s = static_cast<int16_t>(
            static_cast<uint16_t>(riff[i]) |
            (static_cast<uint16_t>(riff[i + 1]) << 8));
        int scaled = (static_cast<int>(s) * volumePct) / 100;
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        const uint16_t u = static_cast<uint16_t>(static_cast<int16_t>(scaled));
        riff[i] = static_cast<uint8_t>(u & 0xFF);
        riff[i + 1] = static_cast<uint8_t>((u >> 8) & 0xFF);
    }
}

// How long a RIFF that assemble() built plays for, in ms - what the worker
// waits out before starting the next clip (one winmm channel: starting it
// early cuts this one off). Reads only the canonical header assemble()
// writes; 0 for anything shorter, so an unknown length never blocks a cue.
inline uint32_t durationMs(const std::vector<uint8_t>& riff) {
    if (riff.size() <= kHeaderSize) return 0;
    const uint32_t rate = static_cast<uint32_t>(riff[24]) |
                          (static_cast<uint32_t>(riff[25]) << 8) |
                          (static_cast<uint32_t>(riff[26]) << 16) |
                          (static_cast<uint32_t>(riff[27]) << 24);
    if (rate == 0) return 0;
    const uint64_t samples = (riff.size() - kHeaderSize) / 2;
    return static_cast<uint32_t>(samples * 1000 / rate);
}

// The five placeholders with their own branch in resolveTokens below - the
// ones a mix could say before every variable could be stitched. Their chunk
// sequences are fixed ("rider" + number, the lap-time oh-oh case) and their
// emptiness rules predate optional groups: {event_rider}, {event_time} and
// {position} are REQUIRED (an unresolvable one empties the whole list so the
// caller falls back rather than skipping words silently), {penalty_seconds}
// and {overtime_laps} OPTIONAL (omitted when absent; see the {penalty_seconds}
// branch).
inline bool isNumericMixToken(const std::string& tok) {
    return tok == "{event_rider}" || tok == "{event_time}" ||
           tok == "{penalty_seconds}" || tok == "{overtime_laps}" ||
           tok == "{position}";
}

// The placeholders a mix recipe may name: the five above, plus EVERY variable
// in spotter_vars.h, which resolveTokens speaks word by word (spokenChunks).
// Anything else that is braced is a typo - and before this check one fell
// through to "a wav filename", became a missing file and dropped the whole
// recipe to TTS: silent on Windows, silence itself under Wine. Not
// hypothetical: the pack generator once baked `{gap_to_behind}` into three
// recipes when the resolver did not know it. Published here so
// SpotterCuePack::parse rejects an unknown one at load, with the pack name and
// the row in the log, instead of leaving it to be inaudible on track.
inline bool isMixToken(const std::string& tok) {
    if (isNumericMixToken(tok)) return true;
    if (tok.size() < 3 || tok.front() != '{' || tok.back() != '}') return false;
    const std::string name = tok.substr(1, tok.size() - 2);
    for (const SpotterVars::Binding& b : SpotterVars::bindings()) {
        if (name == b.name) return true;
    }
    return false;
}

// WORD CHUNKS: how a variable's SPOKEN value becomes clips, so a recorded pack
// can say {trick}, {tier}, gaps, counts and session names, not only the five
// numbers above. The value is the same text the subtitle and TTS get ("double
// backflip", "one point two quicker", "Race 1"), read left to right:
//   - a multi-word entry of SpotterPhrase::kWordKeys plays as ONE clip named
//     after it, spaces as underscores ("flat three sixty" -> flat_three_sixty.wav,
//     "dropped back" -> dropped_back.wav), longest match first;
//   - a run of number words is parsed back to the number numberWords() said
//     and plays num_<N>.wav ("one forty eight" -> num_148.wav, split by the
//     worker's hundreds fallback when the pack has only num_0..99), with the
//     lap-time "one oh oh" played num_1 + oh + oh exactly as {event_time} does;
//   - digits play as the number they are ("Race 1" -> race.wav num_1.wav);
//   - any other word plays <word>.wav, lowercased, apostrophes dropped.
// So the chunk for a word IS the word, which is the convention the five
// placeholders already set (rider.wav, point.wav, laps.wav). A value with a
// word the pack has no clip for - a rider's NAME, a setup name - fails at
// playback like any missing chunk and the cue falls back to TTS rather than
// dropping the word. False only for text no filename can carry (a character
// outside ASCII letters, digits, spaces and . , ' - : or a number past 999).
inline bool spokenChunks(const std::string& spoken, std::vector<std::string>& out) {
    std::vector<std::string> words;
    std::string w;
    for (unsigned char c : spoken) {
        if (std::isalpha(c) || std::isdigit(c)) {
            if (!w.empty() && (std::isdigit(c) != 0) != (std::isdigit(
                    static_cast<unsigned char>(w.back())) != 0)) {
                words.push_back(w);
                w.clear();
            }
            w += static_cast<char>(std::tolower(c));
        } else if (c == '\'') {
            continue;
        } else if (c == ' ' || c == '-') {
            if (!w.empty()) { words.push_back(w); w.clear(); }
        } else if (c == ',' || c == '.' || c == ':') {
            // A pause BETWEEN values: kept as a marker so two numbers either
            // side ("nine, forty") never read as one ("nine forty").
            if (!w.empty()) { words.push_back(w); w.clear(); }
            words.push_back(",");
        } else {
            return false;
        }
    }
    if (!w.empty()) words.push_back(w);

    auto onesValue = [](const std::string& s) -> int {
        for (int i = 0; i < 20; ++i) {
            if (s == SpotterPhrase::kNumberOnes[i]) return i;
        }
        return -1;
    };
    auto tensValue = [](const std::string& s) -> int {
        for (int i = 2; i < 10; ++i) {
            if (s == SpotterPhrase::kNumberTens[i]) return i * 10;
        }
        return -1;
    };
    // A two-digit reading at i ("forty eight", "twelve", "forty"); 0 words
    // consumed when there is none.
    auto twoDigit = [&](size_t i, int& value) -> size_t {
        if (i >= words.size()) return 0;
        const int o = onesValue(words[i]);
        if (o >= 0) { value = o; return 1; }
        const int t = tensValue(words[i]);
        if (t < 0) return 0;
        const int u = i + 1 < words.size() ? onesValue(words[i + 1]) : -1;
        if (u >= 1 && u <= 9) { value = t + u; return 2; }
        value = t;
        return 1;
    };
    auto num = [](int n) { return "num_" + std::to_string(n) + ".wav"; };

    size_t i = 0;
    while (i < words.size()) {
        if (words[i] == ",") { ++i; continue; }
        // Multi-word vocabulary first, longest first, so "flat three sixty"
        // stays the trick and never becomes "flat" + 360.
        size_t bestLen = 0;
        std::string bestName;
        for (const char* key : SpotterPhrase::kWordKeys) {
            std::vector<std::string> parts;
            std::string part;
            for (const char* k = key; ; ++k) {
                if (*k == ' ' || *k == '\0') {
                    parts.push_back(part);
                    part.clear();
                    if (*k == '\0') break;
                } else {
                    part += *k;
                }
            }
            if (parts.size() < 2 || parts.size() <= bestLen ||
                i + parts.size() > words.size()) {
                continue;
            }
            bool match = true;
            for (size_t k = 0; k < parts.size() && match; ++k) {
                match = words[i + k] == parts[k];
            }
            if (!match) continue;
            bestLen = parts.size();
            bestName.clear();
            for (size_t k = 0; k < parts.size(); ++k) {
                if (k) bestName += '_';
                bestName += parts[k];
            }
        }
        if (bestLen) {
            out.push_back(bestName + ".wav");
            i += bestLen;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(words[i][0]))) {
            if (words[i].size() > 3) return false;
            int n = 0;
            for (char d : words[i]) n = n * 10 + (d - '0');
            out.push_back(num(n));
            ++i;
            continue;
        }

        int first = 0;
        const size_t used = twoDigit(i, first);
        if (used == 0) {
            out.push_back(words[i] + ".wav");
            ++i;
            continue;
        }
        i += used;
        // The racing-style hundreds: "four seventy six", "three oh five",
        // "five hundred" - only after a single 1..9 word, as numberWords says
        // them. "oh oh" is the lap time's whole minute (lapTimeNumberWords).
        if (used == 1 && first >= 1 && first <= 9 && i < words.size()) {
            if (words[i] == "hundred") {
                out.push_back(num(first * 100));
                ++i;
                continue;
            }
            if (words[i] == "oh") {
                if (i + 1 < words.size() && words[i + 1] == "oh") {
                    out.push_back(num(first));
                    out.push_back("oh.wav");
                    out.push_back("oh.wav");
                    i += 2;
                    continue;
                }
                const int u = i + 1 < words.size() ? onesValue(words[i + 1]) : -1;
                if (u >= 1 && u <= 9) {
                    out.push_back(num(first * 100 + u));
                    i += 2;
                    continue;
                }
            }
            int rest = 0;
            const size_t more = twoDigit(i, rest);
            if (more && rest >= 10) {
                out.push_back(num(first * 100 + rest));
                i += more;
                continue;
            }
        }
        out.push_back(num(first));
    }
    return true;
}

// Resolve a pack's mix token sequence into the wav filenames to play, in
// order. Tokens are wav names, placeholders (isMixToken) and the optional-
// group markers "[" and "]" (SpotterCuePack::parse splits them off and
// rejects unbalanced or nested ones).
//
// OPTIONAL GROUPS work as they do in a phrase: the group drops whole when any
// placeholder inside it has no value, so `p.wav {position} [ to.wav
// {rider_ahead} ]` says "P four" when you lead. A required placeholder inside a
// group drops the group instead of the whole mix.
//
// The word-chunk placeholders (every variable but the five numeric ones) read
// their value from `vars`; outside a group an empty one says nothing, the way
// it expands to nothing in a phrase. nullptr vars = every one of them empty.
inline std::vector<std::string> resolveTokens(
    const std::vector<std::string>& tokens, int riderNum, int timeComposed,
    int timeTenths, int secs = -1, int laps = -1, int pos = -1,
    const SpotterVars::Vars* vars = nullptr) {
    std::vector<std::string> result;
    std::vector<std::string> group;
    bool inGroup = false;
    bool groupDead = false;
    for (const std::string& tok : tokens) {
        if (tok == "[") {
            inGroup = true;
            groupDead = false;
            group.clear();
            continue;
        }
        if (tok == "]") {
            if (!groupDead) result.insert(result.end(), group.begin(), group.end());
            inGroup = false;
            continue;
        }
        std::vector<std::string>& files = inGroup ? group : result;
        // A placeholder with nothing to say: the group goes, or - outside one -
        // the whole mix, for the required placeholders.
        auto missing = [&]() -> bool {
            if (inGroup) { groupDead = true; return false; }
            return true;
        };
        if (tok == "{event_rider}") {
            if (riderNum < 0 || riderNum > 999) {
                if (missing()) return {};
                continue;
            }
            files.push_back("rider.wav");
            files.push_back("num_" + std::to_string(riderNum) + ".wav");
        } else if (tok == "{event_time}") {
            if (timeComposed < 0 || timeComposed > 999 || timeTenths < 0 ||
                timeTenths > 9) {
                if (missing()) return {};
                continue;
            }
            // Seconds of exactly 00 compose to a multiple of a hundred, and
            // the whole-number clip a full pack recorded for it says "one
            // hundred" — true for rider #100, wrong for 1:00.4, which reads
            // "one oh oh point four". So this one case is stitched rather than
            // played whole, from chunks every pack ships. Mirrors
            // SpotterPhrase::lapTimeNumberWords; the two must agree, because a
            // pack with audio and a pack without must say the same words.
            if (timeComposed >= 100 && timeComposed % 100 == 0) {
                files.push_back("num_" + std::to_string(timeComposed / 100) +
                                ".wav");
                files.push_back("oh.wav");
                files.push_back("oh.wav");
            } else {
                files.push_back("num_" + std::to_string(timeComposed) + ".wav");
            }
            files.push_back("point.wav");
            files.push_back("num_" + std::to_string(timeTenths) + ".wav");
        } else if (tok == "{penalty_seconds}") {
            // OPTIONAL, unlike {event_rider}/{event_time}: a penalty without an amount
            // (older game builds send none) must still play the rest of the
            // recipe, not abort the whole mix — audio is the primary backend
            // (TTS doesn't exist under Wine), so aborting here would turn
            // "no amount known" into "no cue at all".
            if (secs >= 0 && secs <= 999) {
                files.push_back("num_" + std::to_string(secs) + ".wav");
                files.push_back(secs == 1 ? "second.wav" : "seconds.wav");
            } else if (inGroup) {
                groupDead = true;
            }
        } else if (tok == "{overtime_laps}") {
            // Optional like {penalty_seconds}: overtime_started bonus laps.
            if (laps >= 0 && laps <= 999) {
                files.push_back("num_" + std::to_string(laps) + ".wav");
                files.push_back(laps == 1 ? "lap.wav" : "laps.wav");
            } else if (inGroup) {
                groupDead = true;
            }
        } else if (tok == "{position}") {
            // REQUIRED like {event_rider}: "P" with no number is not a cue.
            if (pos < 1 || pos > 999) {
                if (missing()) return {};
                continue;
            }
            files.push_back("num_" + std::to_string(pos) + ".wav");
        } else if (tok.size() > 2 && tok.front() == '{' && tok.back() == '}') {
            // Every other variable, spoken word by word (spokenChunks).
            const std::string* value =
                vars ? SpotterVars::lookup(*vars, tok.substr(1, tok.size() - 2))
                     : nullptr;
            if (!value || value->empty()) {
                if (inGroup) groupDead = true;
                continue;
            }
            std::vector<std::string> words;
            if (!spokenChunks(*value, words)) {
                if (missing()) return {};
                continue;
            }
            files.insert(files.end(), words.begin(), words.end());
        } else {
            files.push_back(tok);
        }
    }
    if (inGroup) return {};  // parse rejects an unclosed group; never guess
    return result;
}

// The hundreds-boundary fallback for a missing three-digit chunk: the chunk
// filenames that together speak the same racing-style reading. Empty when
// `name` is not a decomposable number chunk (not num_*, N < 100, N > 999) —
// two-digit chunks are the atoms, so recursion cannot occur.
inline std::vector<std::string> decomposeNumFile(const std::string& name) {
    if (name.compare(0, 4, "num_") != 0) return {};
    const size_t dot = name.find(".wav");
    if (dot == std::string::npos || dot != name.size() - 4) return {};
    int n = 0;
    for (size_t i = 4; i < dot; ++i) {
        if (name[i] < '0' || name[i] > '9' || dot - 4 > 3) return {};
        n = n * 10 + (name[i] - '0');
    }
    if (dot == 4 || n < 100 || n > 999) return {};

    const int hundreds = n / 100;
    const int rest = n % 100;
    std::vector<std::string> parts;
    parts.push_back("num_" + std::to_string(hundreds) + ".wav");
    if (rest == 0) {
        parts.push_back("hundred.wav");
    } else if (rest < 10) {
        parts.push_back("oh.wav");
        parts.push_back("num_" + std::to_string(rest) + ".wav");
    } else {
        parts.push_back("num_" + std::to_string(rest) + ".wav");
    }
    return parts;
}

// How tight the joins are, in ms, clamped from a pack's `[Mix] gap_ms`.
// The negative half is the interesting one — see assemble().
constexpr int kMinGapMs = -200;
constexpr int kMaxGapMs = 500;

// Assemble chunks into one playable in-memory WAV. All chunks must share one
// sample rate (the packs bake one; a mismatch rejects the whole mix).
//
// gapMs sets the JOIN between consecutive chunks, and its sign changes what
// happens:
//   > 0  that much silence between them (the default, and what a fixed
//        pre-baked recipe wants — a beat between phrases).
//   = 0  butt-joined.
//   < 0  they OVERLAP by |gapMs| with an equal-power crossfade.
//
// The negative half exists because the shipped chunks are already trimmed to
// the speech (measured: <2ms of padding on either end), so zero is as tight as
// concatenation gets — and "nine" "sixty five" butt-joined still reads as two
// words rather than one number. Overlapping borrows the tail of one word under
// the head of the next, which is roughly what a speaker's coarticulation does.
// Equal-power (not linear) because the two are uncorrelated signals: a linear
// fade dips audibly in the middle of the join.
//
// An overlap NEVER eats more than half of either side, so a long |gapMs| with
// short chunks degrades to "as tight as possible" rather than swallowing a
// word whole.
inline std::vector<uint8_t> assemble(const std::vector<Pcm>& chunks,
                                     int gapMs) {
    if (chunks.empty()) return {};
    if (gapMs < kMinGapMs) gapMs = kMinGapMs;
    if (gapMs > kMaxGapMs) gapMs = kMaxGapMs;
    const int rate = chunks[0].sampleRate;
    size_t totalSamples = 0;
    for (const Pcm& c : chunks) {
        if (!c.valid() || c.sampleRate != rate) return {};
        totalSamples += c.samples.size();
    }
    const size_t gapSamples =
        gapMs > 0 ? static_cast<size_t>(gapMs) * static_cast<size_t>(rate) / 1000
                  : 0;
    totalSamples += gapSamples * (chunks.size() - 1);
    // A spotter callout is seconds long; anything past ~2 minutes of audio is
    // a corrupt pack, not a callout. Checked on the pre-overlap length, which
    // is the upper bound — an overlap only ever shortens the result.
    if (totalSamples > static_cast<size_t>(rate) * 120) return {};

    // Lay the samples down first, then wrap them: an overlap rewrites samples
    // already written, which a straight-to-bytes push cannot do.
    std::vector<int16_t> pcm;
    pcm.reserve(totalSamples);
    const size_t overlapSamples =
        gapMs < 0
            ? static_cast<size_t>(-gapMs) * static_cast<size_t>(rate) / 1000
            : 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        const std::vector<int16_t>& s = chunks[i].samples;
        if (i == 0) {
            pcm.insert(pcm.end(), s.begin(), s.end());
            continue;
        }
        const size_t prevLen = chunks[i - 1].samples.size();
        size_t ov = overlapSamples;
        // Clamp against the PREVIOUS chunk, not the whole buffer written so
        // far. pcm.size() grows with every chunk, so bounding by it lets the
        // fade-out reach back through a short middle chunk and into the one
        // before it — at gap_ms = -200 a brief clip ("oh", "point") can be
        // faded away entirely, dropping a word out of a stitched number.
        if (ov > prevLen / 2) ov = prevLen / 2;
        if (ov > s.size() / 2) ov = s.size() / 2;
        if (ov == 0) {
            pcm.insert(pcm.end(), gapSamples, 0);
            pcm.insert(pcm.end(), s.begin(), s.end());
            continue;
        }
        const size_t base = pcm.size() - ov;
        for (size_t k = 0; k < ov; ++k) {
            const float t = (static_cast<float>(k) + 0.5f) /
                            static_cast<float>(ov);
            const float out = std::cos(t * 1.57079633f);   // fade the tail out
            const float in = std::sin(t * 1.57079633f);    // ...the head in
            float v = static_cast<float>(pcm[base + k]) * out +
                      static_cast<float>(s[k]) * in;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            pcm[base + k] = static_cast<int16_t>(v);
        }
        pcm.insert(pcm.end(), s.begin() + static_cast<long>(ov), s.end());
    }

    totalSamples = pcm.size();
    const size_t dataBytes = totalSamples * 2;
    std::vector<uint8_t> wav;
    wav.reserve(kHeaderSize + dataBytes);
    auto push32 = [&](uint32_t v) {
        wav.push_back(static_cast<uint8_t>(v));
        wav.push_back(static_cast<uint8_t>(v >> 8));
        wav.push_back(static_cast<uint8_t>(v >> 16));
        wav.push_back(static_cast<uint8_t>(v >> 24));
    };
    auto push16 = [&](uint16_t v) {
        wav.push_back(static_cast<uint8_t>(v));
        wav.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto pushTag = [&](const char* t) {
        wav.insert(wav.end(), t, t + 4);
    };
    pushTag("RIFF");
    push32(static_cast<uint32_t>(36 + dataBytes));
    pushTag("WAVE");
    pushTag("fmt ");
    push32(16);
    push16(1);                                   // PCM
    push16(1);                                   // mono
    push32(static_cast<uint32_t>(rate));
    push32(static_cast<uint32_t>(rate * 2));     // byte rate
    push16(2);                                   // block align
    push16(16);                                  // bits
    pushTag("data");
    push32(static_cast<uint32_t>(dataBytes));

    for (int16_t s : pcm) push16(static_cast<uint16_t>(s));
    return wav;
}

// Prepend `lead` (a pack's interrupt filler) to a RIFF assemble() built,
// butt-joined: a stumble runs straight on into the call. Leaves `riff` as it
// was when either side is unusable or the rates differ - the call still
// plays, just without its stumble.
inline void prependClip(const Pcm& lead, std::vector<uint8_t>& riff) {
    if (!lead.valid()) return;
    std::vector<Pcm> two(1, lead);
    two.push_back(parseWav(riff.data(), riff.size()));
    if (!two[1].valid()) return;
    std::vector<uint8_t> joined = assemble(two, /*gapMs=*/0);
    if (!joined.empty()) riff = std::move(joined);
}

}  // namespace SpotterMix
