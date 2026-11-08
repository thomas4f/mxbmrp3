// ============================================================================
// core/spotter_position_rows.h
// POSITION ROWS: a pack may give a cue a line for where you are, as
// `<key>_p1`..`<key>_p99` and `<key>_last` (each with its own `_2`.. alternates
// and `_wav`). A RECORDED pack cannot say {position}, so this is how its lap
// report still says "P3": one whole clip per place, never a stitched number.
// `_last` wins when you are last (behind at least one rider - so never alone
// on track), then the row for your place; otherwise the cue's own rows answer,
// so a pack recording P1-P10 says something generic from P11 down. A pack
// with no position rows is unaffected.
//
// Pinned by test_spotter_cue_pack.cpp (the key grammar) and spotter_test.cpp
// (the pick, through emitCue).
// ============================================================================
#pragma once

#include "spotter_cue_pack.h"

#include <string>

namespace SpotterCuePack {

constexpr int kMaxPositionRow = 99;

// Strip a `_p1`..`_p99` or `_last` position suffix (after any variant suffix
// is gone), so `lap_completed_p3` is recognised as its cue.
inline std::string stripPositionSuffix(const std::string& key) {
    static const char kLast[] = "_last";
    const size_t lastLen = sizeof(kLast) - 1;
    if (key.size() > lastLen &&
        key.compare(key.size() - lastLen, lastLen, kLast) == 0) {
        return key.substr(0, key.size() - lastLen);
    }
    const size_t us = key.rfind("_p");
    if (us == std::string::npos || us == 0) return key;
    const size_t digits = key.size() - us - 2;
    if (digits < 1 || digits > 2 || key[us + 2] == '0') return key;
    for (size_t i = us + 2; i < key.size(); ++i) {
        if (key[i] < '0' || key[i] > '9') return key;
    }
    return key.substr(0, us);
}

// The cue a pack row belongs to: variant and position suffixes both gone.
inline std::string cueRoot(const std::string& key) {
    return stripPositionSuffix(stripVariantSuffix(key));
}

inline bool anyPositionRows(const Pack& pack) {
    auto any = [](const auto& m) {
        for (const auto& kv : m) {
            const std::string k = stripVariantSuffix(kv.first);
            if (stripPositionSuffix(k) != k) return true;
        }
        return false;
    };
    return any(pack.phrases) || any(pack.wavs) || any(pack.mixes);
}

// The row set a firing of `key` rolls its alternates from, given your place
// (`position` 1-based, 0 when unknown) and whether that is last. Event-rate
// (a cue firing), not frame-rate, so the two string builds are fine.
inline std::string positionRowKey(const Pack& pack, const std::string& key,
                                  int position, bool isLast) {
    auto defines = [&pack](const std::string& k) {
        return pack.phrases.count(k) || pack.wavs.count(k) ||
               pack.mixes.count(k);
    };
    if (isLast) {
        std::string k = key + "_last";
        if (defines(k)) return k;
    }
    if (position >= 1 && position <= kMaxPositionRow) {
        std::string k = key + "_p" + std::to_string(position);
        if (defines(k)) return k;
    }
    return key;
}

}  // namespace SpotterCuePack
