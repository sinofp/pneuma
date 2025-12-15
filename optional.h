#pragma once

#include "kernel.h"

struct nullopt_t {};

inline constexpr nullopt_t nullopt;

template <typename T> class optional {
    // T is always constructed, this is wrong, but fine because we only use int types
    T v;
    bool has;

public:
    // I don't allow create an empty option this way
    constexpr optional() noexcept = delete;

    constexpr optional(nullopt_t) noexcept
        : v{}
        , has{ false } {}

    optional(T v_)
        : v{ v_ }
        , has{ true } {}

    // optional &operator=(T v_) {
    //     v = v_;
    //     has = true;
    //     return *this;
    // }

    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr bool has_value() const noexcept { return has; }

    constexpr T &value() & {
        if (has_value())
            return v;
        else
            PANIC("bad_optional_access");
    }

    constexpr const T &value() const & {
        if (has_value())
            return v;
        else
            PANIC("bad_optional_access");
    }

    void reset() noexcept {
        if (has_value())
            has = false;
    }
};
