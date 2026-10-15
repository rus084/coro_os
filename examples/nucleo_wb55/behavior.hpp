#pragma once
#include <cstdint>

namespace nucleo {
constexpr std::uint32_t half_period(unsigned led, bool fast) noexcept {
    constexpr std::uint32_t periods[] = {100, 250, 500};
    return periods[led] / (fast ? 2u : 1u);
}
class debounce {
    std::uint32_t since_ = 0;
    bool active_ = false, raw_ = false, reported_ = false;
public:
    void start(std::uint32_t now, bool pressed) noexcept {
        if (active_) return;
        active_ = true; raw_ = pressed; reported_ = false; since_ = now;
    }
    bool sample(std::uint32_t now, bool pressed) noexcept {
        if (!active_) return false;
        if (pressed != raw_) { raw_ = pressed; since_ = now; }
        if (now - since_ < 30u) return false;
        if (!pressed) active_ = false;
        else if (!reported_) { reported_ = true; return true; }
        return false;
    }
    bool active() const noexcept { return active_; }
};
} // namespace nucleo
