#pragma once
#include "../port.hpp"

#if !defined(__arm__) && !defined(__thumb__)
#error "The Cortex-M port requires an ARM Thumb target"
#endif

namespace coro_os::ports::cortex_m {
inline std::uint32_t primask() noexcept {
    std::uint32_t value;
    asm volatile("mrs %0, primask" : "=r"(value) :: "memory");
    return value;
}
inline std::uint32_t enter_critical() noexcept {
    std::uint32_t saved;
    asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(saved) :: "memory");
    return saved;
}
inline void exit_critical(std::uint32_t saved) noexcept {
    asm volatile("msr primask, %0" :: "r"(saved) : "memory");
}
inline void notify_cpu() noexcept { asm volatile("sev" ::: "memory"); }
inline bool in_isr() noexcept {
    std::uint32_t value;
    asm volatile("mrs %0, ipsr" : "=r"(value) :: "memory");
    return value != 0;
}
struct port {
    static std::uint32_t enter_critical() noexcept { return cortex_m::enter_critical(); }
    static void exit_critical(std::uint32_t saved) noexcept { cortex_m::exit_critical(saved); }
    static void notify_cpu() noexcept { cortex_m::notify_cpu(); }
    static bool in_isr() noexcept { return cortex_m::in_isr(); }
};
template<class Ready> void idle_if(Ready ready) noexcept {
    coro_os::detail::require(primask() == 0 && !in_isr());
    // Clear a stale event before checking work. A new SEV after the check
    // prevents the final WFE from sleeping, so an IRQ cannot be lost.
    asm volatile("sev\n\twfe" ::: "memory");
    if (!ready()) asm volatile("wfe" ::: "memory");
}
} // namespace coro_os::ports::cortex_m
