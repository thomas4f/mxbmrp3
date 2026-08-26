// ============================================================================
// core/small_vec.h
// A tiny sequence with INLINE storage for the first N elements and a heap spill
// beyond them -- for the short lists a hot path builds and throws away every
// frame, where the allocation costs more than the work.
//
// WHY THIS EXISTS AND NOT std::vector. Measured on a reporting user's machine,
// inside the game process: ONE std::vector alloc+free round-trip costs 1.57us.
// (Headless under Wine the same probe reads 0.22us, which is why this was
// invisible in the harness for three sessions -- a game's process heap is
// contended and fragmented in a way the test driver's is not.) BaseHud::PanelWant
// is built and destroyed once per HUD per rebuild, six HUDs a frame, so a single
// vector member in it cost ~9us/frame of a 2083us budget -- more than the layout
// it describes. The probe that measured it was scratch and has been removed; its
// numbers are in the commit that added it.
//
// WHY NOT AN OFF-THE-SHELF ONE. C++17's standard library has no fixed-capacity
// or small-buffer sequence: std::inplace_vector is C++26 and, being purely
// inline, would turn an over-capacity list into undefined behaviour rather than
// a slow path. boost::container::small_vector and llvm::SmallVector are both
// exactly this container, and vendoring either whole for one field of one struct
// costs more than the fifty lines below. Delete this the day the project moves to
// C++26 AND every caller's length is provably bounded.
//
// THE SPILL IS THE POINT. Over capacity it degrades to a plain std::vector rather
// than truncating: a dropped element here is a section the panel does not
// reserve height for, which is a layout bug, not a slow frame. Capacity is a
// performance choice, never a correctness one.
// ============================================================================
#pragma once

#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

// Trivially-copyable T only: the inline buffer is default-constructed whole and
// assigned over, which is free for scalars and wrong for anything owning memory.
template <typename T, std::size_t N>
class SmallVec {
    static_assert(std::is_trivially_copyable<T>::value,
                  "SmallVec stores T in a default-constructed inline buffer");
    static_assert(N > 0, "SmallVec needs inline capacity to be worth using");

public:
    using value_type = T;

    SmallVec() = default;
    SmallVec(std::initializer_list<T> il) { *this = il; }

    SmallVec(const SmallVec&) = default;
    SmallVec& operator=(const SmallVec&) = default;
    // User-provided moves, because the implicit pair breaks the "m_spill
    // non-empty IS the spilled flag" invariant on the SOURCE: it moves m_spill
    // (leaving it empty) while COPYING m_size, so a spilled moved-from SmallVec
    // reported size() > N with data() back on the inline buffer -- iterating it
    // read out of bounds. Zeroing the source's size restores the valid state
    // every other member assumes.
    SmallVec(SmallVec&& o) noexcept
        : m_size(o.m_size), m_spill(std::move(o.m_spill)) {
        for (std::size_t i = 0; i < N; ++i) m_inline[i] = o.m_inline[i];
        o.m_size = 0;
        o.m_spill.clear();   // moved-from vector is valid but not GUARANTEED empty
    }
    SmallVec& operator=(SmallVec&& o) noexcept {
        if (this != &o) {
            for (std::size_t i = 0; i < N; ++i) m_inline[i] = o.m_inline[i];
            m_size = o.m_size;
            m_spill = std::move(o.m_spill);
            o.m_size = 0;
            o.m_spill.clear();
        }
        return *this;
    }

    SmallVec& operator=(std::initializer_list<T> il) {
        clear();
        for (const T& v : il) push_back(v);
        return *this;
    }

    void clear() { m_size = 0; m_spill.clear(); }

    void push_back(const T& v) {
        // m_spill non-empty IS the spilled flag -- the two are set together and
        // cleared together, so there is no third state to keep in step.
        if (m_spill.empty()) {
            if (m_size < N) { m_inline[m_size++] = v; return; }
            m_spill.assign(m_inline, m_inline + N);
        }
        m_spill.push_back(v);
        ++m_size;
    }

    std::size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    const T* data() const { return m_spill.empty() ? m_inline : m_spill.data(); }
    T* data() { return m_spill.empty() ? m_inline : m_spill.data(); }

    const T& operator[](std::size_t i) const { return data()[i]; }
    T& operator[](std::size_t i) { return data()[i]; }
    const T& back() const { return data()[m_size - 1]; }

    const T* begin() const { return data(); }
    const T* end() const { return data() + m_size; }
    T* begin() { return data(); }
    T* end() { return data() + m_size; }

    bool operator==(const SmallVec& o) const {
        if (m_size != o.m_size) return false;
        for (std::size_t i = 0; i < m_size; ++i)
            if (!(data()[i] == o.data()[i])) return false;
        return true;
    }
    bool operator!=(const SmallVec& o) const { return !(*this == o); }

private:
    T m_inline[N] = {};
    std::size_t m_size = 0;
    std::vector<T> m_spill;   // empty unless size has exceeded N
};
