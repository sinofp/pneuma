#pragma once
#include "common.h"

template <typename T> class span {
    const T *p;
    size_t n;

public:
    span(const T *pointer, size_t size)
        : p(pointer)
        , n(size) {}

    [[nodiscard]]
    constexpr size_t size() const {
        return n;
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
