// ============================================================================
// core/flat_int_map.h
// FlatIntSet / FlatIntMap<V>: a set and a map keyed by int, stored as one
// sorted vector. For per-rider containers that are CLEARED AND REFILLED on a
// hot path (a track-position batch every frame, a position cache on every
// classification): std::unordered_set/map free every node on clear() and
// allocate one per insert, so a 50-rider refill was 50 heap allocations per
// call. A vector keeps its capacity across clear(), so after the first fill a
// refill allocates nothing. Lookups are a binary search over at most
// MAX_RACE_ENTRIES ints, which is cheaper than hashing at that size.
//
// The interface is the subset of the std containers the callers use, under the
// same names, so a PerRider<> declaration can swap one in unchanged (PerRider
// needs erase(key) and clear()). Iteration is in key order.
// Pinned by tests/unit/test_flat_int_map.cpp; the allocation count is the perf
// ledger's (cb.RaceTrackPosition.allocs, run_perf_ledger.sh).
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

class FlatIntSet {
public:
    using const_iterator = std::vector<int>::const_iterator;

    // true when the key was not there yet (std::set's insert().second).
    bool insert(int key) {
        auto it = std::lower_bound(m_keys.begin(), m_keys.end(), key);
        if (it != m_keys.end() && *it == key) return false;
        m_keys.insert(it, key);
        return true;
    }
    size_t erase(int key) {
        auto it = std::lower_bound(m_keys.begin(), m_keys.end(), key);
        if (it == m_keys.end() || *it != key) return 0;
        m_keys.erase(it);
        return 1;
    }
    size_t count(int key) const { return std::binary_search(m_keys.begin(), m_keys.end(), key) ? 1 : 0; }
    void clear() { m_keys.clear(); }      // keeps the capacity: the point of this class
    void reserve(size_t n) { m_keys.reserve(n); }
    size_t size() const { return m_keys.size(); }
    bool empty() const { return m_keys.empty(); }
    const_iterator begin() const { return m_keys.begin(); }
    const_iterator end() const { return m_keys.end(); }

private:
    std::vector<int> m_keys;
};

template <typename V>
class FlatIntMap {
public:
    using value_type = std::pair<int, V>;
    using iterator = typename std::vector<value_type>::iterator;
    using const_iterator = typename std::vector<value_type>::const_iterator;

    V& operator[](int key) {
        auto it = lowerBound(key);
        if (it == m_items.end() || it->first != key) it = m_items.insert(it, value_type(key, V{}));
        return it->second;
    }
    iterator find(int key) {
        auto it = lowerBound(key);
        return (it != m_items.end() && it->first == key) ? it : m_items.end();
    }
    const_iterator find(int key) const {
        auto it = std::lower_bound(m_items.begin(), m_items.end(), key,
                                   [](const value_type& a, int k) { return a.first < k; });
        return (it != m_items.end() && it->first == key) ? it : m_items.end();
    }
    size_t count(int key) const { return find(key) != m_items.end() ? 1 : 0; }
    size_t erase(int key) {
        auto it = find(key);
        if (it == m_items.end()) return 0;
        m_items.erase(it);
        return 1;
    }
    void clear() { m_items.clear(); }    // keeps the capacity: the point of this class
    void swap(FlatIntMap& other) noexcept { m_items.swap(other.m_items); }  // buffers trade places, none freed
    void reserve(size_t n) { m_items.reserve(n); }
    size_t size() const { return m_items.size(); }
    bool empty() const { return m_items.empty(); }
    iterator begin() { return m_items.begin(); }
    iterator end() { return m_items.end(); }
    const_iterator begin() const { return m_items.begin(); }
    const_iterator end() const { return m_items.end(); }

private:
    iterator lowerBound(int key) {
        return std::lower_bound(m_items.begin(), m_items.end(), key,
                                [](const value_type& a, int k) { return a.first < k; });
    }
    std::vector<value_type> m_items;
};
