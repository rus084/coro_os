#pragma once
#include <cstdint>
#include "stm32wbxx.h"
#include "behavior.hpp"

namespace nucleo {
struct debug_info {
    std::uint32_t signature, variant, irq_count, press_count, fast;
    std::uint32_t toggles[3], fault;
};
extern "C" volatile debug_info debug_state;
void init() noexcept;
void enable_button_irq() noexcept;
void start_ticks() noexcept;
void tick_irq() noexcept;
std::uint32_t ticks() noexcept;
bool button_pressed() noexcept;
void led_toggle(unsigned led) noexcept;
void accept_press() noexcept;
bool fast() noexcept;
void button_irq() noexcept; // Supplied by each variant.
inline void idle() noexcept { __DSB(); __WFE(); }
} // namespace nucleo
