// ============================================================================
// tests/unit/test_flat_int_map.cpp
// FlatIntSet / FlatIntMap (core/flat_int_map.h): the sorted-vector stand-ins
// for unordered_set/map on per-rider containers refilled every frame.
//
// What has to hold for the swap to be invisible to its callers: the std
// semantics of insert/erase/count/find/operator[] (including the return
// values), key-ordered iteration however the keys arrived, and - the reason
// the class exists - that a clear() and refill of the same size does not
// reallocate (the perf ledger counts that as cb.RaceTrackPosition.allocs).
// ============================================================================
#include "doctest.h"

#include "core/flat_int_map.h"

#include <vector>

TEST_CASE("FlatIntSet keeps std::set semantics in key order") {
    FlatIntSet s;
    CHECK(s.empty());
    CHECK(s.insert(42));
    CHECK(s.insert(7));
    CHECK(s.insert(99));
    CHECK_FALSE(s.insert(7));                  // duplicate: not inserted again
    CHECK(s.size() == 3);
    CHECK(s.count(42) == 1);
    CHECK(s.count(8) == 0);
    CHECK(std::vector<int>(s.begin(), s.end()) == std::vector<int>{7, 42, 99});
    CHECK(s.erase(42) == 1);
    CHECK(s.erase(42) == 0);
    CHECK(s.count(42) == 0);
    s.clear();
    CHECK(s.empty());
    CHECK(s.count(7) == 0);
}

TEST_CASE("FlatIntSet refill after clear() does not reallocate") {
    FlatIntSet s;
    for (int i = 50; i > 0; --i) s.insert(i * 3);
    const int* before = &*s.begin();
    s.clear();
    for (int i = 1; i <= 50; ++i) s.insert(i * 7);
    CHECK(&*s.begin() == before);              // same buffer: no allocation
    CHECK(s.size() == 50);
}

TEST_CASE("FlatIntMap keeps std::map semantics in key order") {
    FlatIntMap<int> m;
    m[30] = 3;
    m[10] = 1;
    m[20] = 2;
    m[10] = 11;                                // overwrite, no second entry
    CHECK(m.size() == 3);
    REQUIRE(m.find(10) != m.end());
    CHECK(m.find(10)->second == 11);
    CHECK(m.find(15) == m.end());
    CHECK(m.count(20) == 1);
    CHECK(m[25] == 0);                         // operator[] value-initialises
    CHECK(m.size() == 4);
    std::vector<int> keys;
    for (const auto& kv : m) keys.push_back(kv.first);
    CHECK(keys == std::vector<int>{10, 20, 25, 30});
    CHECK(m.erase(20) == 1);
    CHECK(m.erase(20) == 0);
    const FlatIntMap<int>& cm = m;
    CHECK(cm.find(30)->second == 3);
    CHECK(cm.find(20) == cm.end());
    m.clear();
    CHECK(m.empty());
}
