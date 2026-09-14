#pragma once
#include "lib/types.h"

template <typename T> class span {
    T *p;
    size_t n;

public:
    constexpr span()
        : p(nullptr)
        , n(0) {}

    constexpr span(T *pointer, size_t size)
        : p(pointer)
        , n(size) {}

    [[nodiscard]]
    constexpr size_t size() const {
        return n;
    }

    [[nodiscard]]
    constexpr T *data() {
        return p;
    }

    [[nodiscard]]
    constexpr const T *data() const {
        return p;
    }

    [[nodiscard]]
    constexpr span<T> subspan(size_t offset, size_t count) const {
        // TODO bound checking and contract violation
        return { p + offset, count };
    }

    // span is TriviallyCopyable, so no need to disable copy ctor.
    // It should be able to be moved, but we don't need it
};
