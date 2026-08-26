// ============================================================================
// tests/unit/test_spotter_mix.cpp
// Pins the spotter chunk mixer (core/spotter_mix.h). Two distinct jobs:
//
//  1. parseWav is a TRUST BOUNDARY — chunk wavs arrive in shared packs, so
//     the malformed cases here (lying sizes, truncation, stereo/compressed,
//     data-before-fmt) must reject WHOLE, and the unit-asan flavor is what
//     gives the bounds checks teeth (an OOB read is a red gate, not luck).
//  2. Token resolution + assembly are the callout contract: {event_rider}/{event_time}
//     map to the frozen num_/rider/point chunk names, an unresolvable
//     placeholder empties the WHOLE list (fall back to TTS, never skip words
//     silently), and the assembled RIFF is exactly what PlaySound needs —
//     sizes consistent, gaps between chunks, one sample rate enforced.
// ============================================================================
#include "doctest.h"

#include "core/spotter_mix.h"

using namespace SpotterMix;

namespace {

// Build a valid PCM16 mono wav with the given sample values.
std::vector<uint8_t> makeWav(const std::vector<int16_t>& samples,
                             int rate = 24000) {
    std::vector<Pcm> one(1);
    one[0].samples = samples;
    one[0].sampleRate = rate;
    return assemble(one, 0);
}

}  // namespace

TEST_CASE("parseWav: round-trips what assemble builds") {
    const std::vector<int16_t> s = {0, 1000, -1000, 32767, -32768};
    const auto wav = makeWav(s);
    const Pcm p = parseWav(wav.data(), wav.size());
    REQUIRE(p.valid());
    CHECK(p.sampleRate == 24000);
    CHECK(p.samples == s);
}

TEST_CASE("parseWav: rejects malformed input whole") {
    const auto good = makeWav({1, 2, 3, 4});

    CHECK_FALSE(parseWav(nullptr, 44).valid());
    CHECK_FALSE(parseWav(good.data(), 10).valid());       // truncated header
    CHECK_FALSE(parseWav(good.data(), good.size() - 3).valid());  // truncated data

    auto bad = good;
    bad[0] = 'X';                                          // wrong magic
    CHECK_FALSE(parseWav(bad.data(), bad.size()).valid());

    bad = good;
    bad[22] = 2;                                           // stereo
    CHECK_FALSE(parseWav(bad.data(), bad.size()).valid());

    bad = good;
    bad[20] = 3;                                           // IEEE float format
    CHECK_FALSE(parseWav(bad.data(), bad.size()).valid());

    bad = good;
    bad[34] = 8;                                           // 8-bit samples
    CHECK_FALSE(parseWav(bad.data(), bad.size()).valid());

    // data chunk lying about its size (points past the buffer end).
    bad = good;
    bad[40] = 0xFF;
    bad[41] = 0xFF;
    CHECK_FALSE(parseWav(bad.data(), bad.size()).valid());
}

TEST_CASE("resolveTokens: placeholders map to the frozen chunk names") {
    const std::vector<std::string> tokens = {"fl_head.wav", "{event_rider}",
                                             "{event_time}"};
    const auto files = resolveTokens(tokens, 476, 148, 2);
    REQUIRE(files.size() == 6);
    CHECK(files[0] == "fl_head.wav");
    CHECK(files[1] == "rider.wav");
    CHECK(files[2] == "num_476.wav");
    CHECK(files[3] == "num_148.wav");
    CHECK(files[4] == "point.wav");
    CHECK(files[5] == "num_2.wav");
}

// The one lap time that is NOT its whole-number clip. A pack that recorded
// num_100.wav recorded the word "hundred" in it — correct for rider #100, and
// wrong for 1:00.4, which a spotter reads "one oh oh point four". Stitched from
// chunks every pack ships instead, and it has to match the TTS side exactly
// (SpotterPhrase::lapTimeNumberWords) or the same cue says two different times
// depending on whether the listener downloaded a voice pack.
TEST_CASE("resolveTokens: a lap time on the minute is stitched, not the clip") {
    const auto files = resolveTokens({"{event_time}"}, -1, 100, 4);
    REQUIRE(files.size() == 5);
    CHECK(files[0] == "num_1.wav");
    CHECK(files[1] == "oh.wav");
    CHECK(files[2] == "oh.wav");     // "one oh oh"
    CHECK(files[3] == "point.wav");
    CHECK(files[4] == "num_4.wav");

    // Two minutes dead, same shape.
    const auto two = resolveTokens({"{event_time}"}, -1, 200, 3);
    REQUIRE(two.size() == 5);
    CHECK(two[0] == "num_2.wav");
    CHECK(two[1] == "oh.wav");
    CHECK(two[2] == "oh.wav");

    // A rider number is untouched: 100 there really is "one hundred", and its
    // whole-number clip is the right one to play.
    const auto rider = resolveTokens({"{event_rider}"}, 100, -1, -1);
    REQUIRE(rider.size() == 2);
    CHECK(rider[1] == "num_100.wav");
}

TEST_CASE("resolveTokens: unresolvable placeholder empties the whole list") {
    // No rider for a {event_rider} template: TTS fallback, never silent word-skip.
    CHECK(resolveTokens({"a.wav", "{event_rider}"}, -1, 148, 2).empty());
    CHECK(resolveTokens({"a.wav", "{event_rider}"}, 1000, 148, 2).empty());
    // No time for a {event_time} template.
    CHECK(resolveTokens({"{event_time}"}, 5, -1, 2).empty());
    CHECK(resolveTokens({"{event_time}"}, 5, 148, -1).empty());
    // No placeholders: passthrough regardless of values.
    const auto files = resolveTokens({"x.wav"}, -1, -1, -1);
    REQUIRE(files.size() == 1);
    CHECK(files[0] == "x.wav");
}

TEST_CASE("resolveTokens: {penalty_seconds} is optional — omitted, never aborting") {
    // Penalty seconds present: number chunk + the plural/singular word.
    auto files = resolveTokens({"pen.wav", "{penalty_seconds}"}, -1, -1, -1, 5);
    REQUIRE(files.size() == 3);
    CHECK(files[0] == "pen.wav");
    CHECK(files[1] == "num_5.wav");
    CHECK(files[2] == "seconds.wav");
    files = resolveTokens({"pen.wav", "{penalty_seconds}"}, -1, -1, -1, 1);
    REQUIRE(files.size() == 3);
    CHECK(files[2] == "second.wav");
    // Absent: the rest of the recipe still plays — a penalty with no amount
    // (older game builds) must keep its base call, not fall to TTS (which
    // is silence under Wine).
    files = resolveTokens({"pen.wav", "{event_rider}", "{penalty_seconds}"}, 42, -1, -1, -1);
    REQUIRE(files.size() == 3);
    CHECK(files[0] == "pen.wav");
    CHECK(files[1] == "rider.wav");
    CHECK(files[2] == "num_42.wav");
}

TEST_CASE("resolveTokens: {overtime_laps} optional suffix, {position} required number") {
    // Overtime bonus laps: number + plural/singular suffix; absent = omit.
    // "laps", not "laps to go" — the cue says "two laps AFTER THIS ONE",
    // because no countdown is true wherever the leader is when the clock
    // expires (SpotterPhrase::lapsWords). A pack's own recipe supplies the
    // rest of the sentence, so the token stays the number and its plural.
    auto files = resolveTokens({"ot.wav", "{overtime_laps}"}, -1, -1, -1, -1, 2);
    REQUIRE(files.size() == 3);
    CHECK(files[1] == "num_2.wav");
    CHECK(files[2] == "laps.wav");
    files = resolveTokens({"ot.wav", "{overtime_laps}"}, -1, -1, -1, -1, 1);
    REQUIRE(files.size() == 3);
    CHECK(files[2] == "lap.wav");
    files = resolveTokens({"ot.wav", "{overtime_laps}"}, -1, -1, -1, -1, -1);
    REQUIRE(files.size() == 1);

    // Position: "P" with no number is not a cue — required, like {event_rider}.
    files = resolveTokens({"seg_p.wav", "{position}"}, -1, -1, -1, -1, -1, 3);
    REQUIRE(files.size() == 2);
    CHECK(files[1] == "num_3.wav");
    CHECK(resolveTokens({"seg_p.wav", "{position}"}, -1, -1, -1, -1, -1, -1)
              .empty());
    CHECK(resolveTokens({"seg_p.wav", "{position}"}, -1, -1, -1, -1, -1, 0)
              .empty());
}

TEST_CASE("decomposeNumFile: hundreds split along the racing-style reading") {
    // 142 -> "one | forty two": the pair chunk IS num_42, no new names.
    auto p = decomposeNumFile("num_142.wav");
    REQUIRE(p.size() == 2);
    CHECK(p[0] == "num_1.wav");
    CHECK(p[1] == "num_42.wav");
    // Whole hundreds and the oh-case need the two extra vocabulary words.
    p = decomposeNumFile("num_500.wav");
    REQUIRE(p.size() == 2);
    CHECK(p[1] == "hundred.wav");
    p = decomposeNumFile("num_305.wav");
    REQUIRE(p.size() == 3);
    CHECK(p[0] == "num_3.wav");
    CHECK(p[1] == "oh.wav");
    CHECK(p[2] == "num_5.wav");
    // Atoms and non-number chunks don't decompose (this is what makes the
    // worker's fallback recursion-free).
    CHECK(decomposeNumFile("num_42.wav").empty());
    CHECK(decomposeNumFile("num_0.wav").empty());
    CHECK(decomposeNumFile("rider.wav").empty());
    CHECK(decomposeNumFile("num_1000.wav").empty());
    CHECK(decomposeNumFile("num_.wav").empty());
    CHECK(decomposeNumFile("num_12x.wav").empty());
    CHECK(decomposeNumFile("num_142.WAV").empty());
}

TEST_CASE("applyGain: scales samples, leaves the header alone") {
    // The wav backends have no per-sound volume of their own (PlaySound
    // gives none), so this is what makes the [Spotter] volume slider move
    // anything on the DEFAULT audio path — packs, not TTS.
    std::vector<Pcm> chunks(1);
    chunks[0].samples = {1000, -1000, 32767, -32768};
    chunks[0].sampleRate = 8000;
    std::vector<uint8_t> riff = assemble(chunks, 0);
    REQUIRE(riff.size() == kHeaderSize + 8);
    const std::vector<uint8_t> header(riff.begin(),
                                      riff.begin() + kHeaderSize);

    applyGain(riff, 50);
    // Header untouched: sizes and format still describe the same buffer.
    CHECK(std::vector<uint8_t>(riff.begin(), riff.begin() + kHeaderSize) ==
          header);
    Pcm back = parseWav(riff.data(), riff.size());
    REQUIRE(back.valid());
    REQUIRE(back.samples.size() == 4);
    CHECK(back.samples[0] == 500);
    CHECK(back.samples[1] == -500);
    CHECK(back.samples[2] == 16383);   // full scale halves without clipping
    CHECK(back.samples[3] == -16384);

    // 100 is a no-op fast path; 0 is silence, not garbage.
    std::vector<uint8_t> full = assemble(chunks, 0);
    const std::vector<uint8_t> before = full;
    applyGain(full, 100);
    CHECK(full == before);
    applyGain(full, 0);
    Pcm silent = parseWav(full.data(), full.size());
    REQUIRE(silent.valid());
    for (int16_t s : silent.samples) CHECK(s == 0);

    // A buffer too small to hold samples must not be walked past its end
    // (ASan flavour is the gate with teeth here).
    std::vector<uint8_t> stub(kHeaderSize, 0);
    applyGain(stub, 50);
    CHECK(stub.size() == kHeaderSize);
}

TEST_CASE("assemble: concatenates with gaps, enforces one sample rate") {
    std::vector<Pcm> chunks(2);
    chunks[0].samples = {10, 20};
    chunks[0].sampleRate = 1000;   // 1kHz: 50ms gap = 50 samples
    chunks[1].samples = {30};
    chunks[1].sampleRate = 1000;

    const auto wav = assemble(chunks, 50);
    const Pcm p = parseWav(wav.data(), wav.size());
    REQUIRE(p.valid());
    REQUIRE(p.samples.size() == 2 + 50 + 1);
    CHECK(p.samples[0] == 10);
    CHECK(p.samples[1] == 20);
    CHECK(p.samples[2] == 0);      // gap silence
    CHECK(p.samples[51] == 0);
    CHECK(p.samples[52] == 30);

    chunks[1].sampleRate = 2000;   // mismatched rate rejects the whole mix
    CHECK(assemble(chunks, 50).empty());

    chunks[1].sampleRate = 1000;
    chunks[1].samples.clear();     // an invalid chunk rejects the whole mix
    CHECK(assemble(chunks, 50).empty());
}

// The negative half of gapMs, which is what makes a stitched number read as one
// word. Every shipped pack carries num_0..99 only, so EVERY three-digit rider
// number is a join ("nine" + "sixty five") — this is the common path, not a
// corner.
TEST_CASE("assemble: a negative gap overlaps the join instead of padding it") {
    std::vector<Pcm> chunks(2);
    chunks[0].sampleRate = 1000;   // 1kHz: 1 sample per ms, so ms == samples
    chunks[1].sampleRate = 1000;
    chunks[0].samples.assign(200, 1000);
    chunks[1].samples.assign(200, 1000);

    const Pcm butt = parseWav(assemble(chunks, 0).data(),
                              assemble(chunks, 0).size());
    REQUIRE(butt.valid());
    CHECK(butt.samples.size() == 400);

    const std::vector<uint8_t> raw = assemble(chunks, -40);
    const Pcm ov = parseWav(raw.data(), raw.size());
    REQUIRE(ov.valid());
    CHECK(ov.samples.size() == 400 - 40);

    // Equal-power, not linear: two uncorrelated signals crossfaded linearly dip
    // in the middle of the join, which is audible as a hole between the words.
    // Here both sides are the same level, so a correct crossfade holds it.
    const size_t joinMid = 200 - 20;
    CHECK(ov.samples[joinMid] > 900);
    CHECK(ov.samples[joinMid] <= 1500);
}

TEST_CASE("assemble: an overlap never eats more than half of either side") {
    std::vector<Pcm> chunks(2);
    chunks[0].sampleRate = 1000;
    chunks[1].sampleRate = 1000;
    chunks[0].samples.assign(200, 500);
    chunks[1].samples.assign(20, 500);     // a short chunk beside a long one

    // Asking to overlap 120ms into a 20ms chunk must not swallow it: the
    // shipped chunks are ~350-800ms, but a pack of clipped one-syllable
    // recordings is exactly what a too-eager pack ini would destroy.
    const std::vector<uint8_t> raw = assemble(chunks, -120);
    const Pcm p = parseWav(raw.data(), raw.size());
    REQUIRE(p.valid());
    CHECK(p.samples.size() == 200 + 20 - 10);   // capped at half of 20

    // Out-of-range values clamp rather than doing something wild.
    const std::vector<uint8_t> huge = assemble(chunks, -100000);
    const Pcm hp = parseWav(huge.data(), huge.size());
    REQUIRE(hp.valid());
    CHECK(hp.samples.size() == 200 + 20 - 10);
    const std::vector<uint8_t> wide = assemble(chunks, 100000);
    const Pcm wp = parseWav(wide.data(), wide.size());
    REQUIRE(wp.valid());
    CHECK(wp.samples.size() == 200 + 20 + kMaxGapMs);

    // A single chunk has no join at all, whatever the setting.
    chunks.pop_back();
    const std::vector<uint8_t> one = assemble(chunks, -80);
    const Pcm op = parseWav(one.data(), one.size());
    REQUIRE(op.valid());
    CHECK(op.samples.size() == 200);
}
