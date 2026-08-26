// ============================================================================
// core/history_ring.h
// A fixed-capacity, CONTIGUOUS rolling history: push a sample, read the last N
// in order, never touch the heap.
//
// WHY THIS EXISTS. The telemetry and rumble graphs kept their histories in
// std::deque<float> and walked them BY INDEX every rebuild. Two costs came with
// that, and together they made those two the most expensive panels in the plugin
// (17.3 and 9.4 us/frame, measured in game):
//
//  * INDEXING A DEQUE IS A POINTER CHASE. On the MSVC CRT the block size is
//    `sizeof(T) <= 4 ? 4 : ...` elements, so a deque<float> holds FOUR floats per
//    heap block -- a 200-sample channel is 50 separate allocations scattered
//    across the heap. operator[] resolves a block pointer and then an offset, and
//    the walk defeats prefetching completely. TelemetryHud does ~1600 of those
//    per rebuild (199 steps x 4 enabled channels x 2 endpoints).
//  * EVERY SAMPLE TOUCHES THE ALLOCATOR. push_back + pop_front at the cap frees a
//    block every fourth sample, on the game thread, where a heap round-trip was
//    measured at 1.5-3.4us (see small_vec.h).
//
// PerformanceHud draws the same picture from a std::array and costs a fraction of
// the same work per point -- it was never simpler, just contiguous. This is that
// storage, made reusable.
//
// WHY NOT std::vector or a plain array. A vector would still need an erase-front
// (O(n) memmove per sample) or a hand-rolled index; the ring is the index, done
// once, in a type that cannot be used wrongly. Capacity is a template parameter so
// the buffer is a member -- no allocation at construction either.
//
// NOT THREAD-SAFE, deliberately: every current writer (the telemetry handler, the
// rumble update it calls) is on the game thread, same as the HUDs that read.
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

// T must be trivially copyable: the buffer is default-constructed whole and
// assigned over, which is free for the samples this holds and wrong for anything
// owning memory.
template <typename T, std::size_t N>
class HistoryRing {
    static_assert(std::is_trivially_copyable<T>::value,
                  "HistoryRing stores T in a default-constructed inline buffer");
    static_assert(N > 1, "a history of one sample draws no line");

public:
    using value_type = T;

    static constexpr std::size_t capacity() { return N; }

    // Append, dropping the oldest once full.
    void push(const T& v) {
        if (m_size < N) {
            m_data[wrap(m_head + m_size)] = v;
            ++m_size;
        } else {
            m_data[m_head] = v;          // the oldest slot becomes the newest
            m_head = wrap(m_head + 1);
        }
    }

    std::size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    // 0 is the OLDEST sample, size()-1 the newest -- the order the graphs draw in,
    // so a caller never has to know where the ring's seam is.
    const T& operator[](std::size_t i) const { return m_data[wrap(m_head + i)]; }
    const T& back() const { return m_data[wrap(m_head + m_size - 1)]; }

    void clear() { m_size = 0; m_head = 0; }

private:
    // A conditional subtract, not a modulo: the sum is always below 2N, and this
    // sits in the innermost loop of every strip chart.
    static std::size_t wrap(std::size_t i) { return (i >= N) ? (i - N) : i; }

    std::array<T, N> m_data{};
    std::size_t m_size = 0;
    std::size_t m_head = 0;   // index of the oldest sample
};
