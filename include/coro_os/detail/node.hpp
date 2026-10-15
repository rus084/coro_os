#pragma once
#include <cstdint>

namespace coro_os {
using tick_t = std::uint32_t;
inline constexpr tick_t no_deadline = UINT32_MAX;
enum class wait_result { notified, timeout };
template<class Port> class scheduler;

namespace detail {
// Shared across every Port specialization and every translation unit.
inline constinit bool scheduler_active = false;
inline void require(bool condition) noexcept {
    if (!condition) __builtin_trap();
}
enum class state : std::uint8_t { detached, ready, running, waiting, sleeping, timed_wait, blocked, timed_blocked, completed };
template<class Port> struct wait_record;

// One shared byte. After publication, every read/write requires the port's
// critical guard. Keep thread-only queue flags in separate memory locations.
struct irq_state {
    std::uint8_t in_pending : 1 = false;
    std::uint8_t token : 1 = false;
    std::uint8_t accepts_signals : 1 = false;
    std::uint8_t wake_requested : 1 = false;
};
static_assert(sizeof(irq_state) == 1);
template<class Port> struct node {
    // A node belongs to either ready or timer, never both. ISR never touches it.
    node* queue_next = nullptr;
    node* pending_next = nullptr;
    node* pending_prev = nullptr;
    tick_t delta = 0;
    state status = state::detached;
    bool in_ready = false;
    bool in_timer = false;
    irq_state irq;
    void* task = nullptr;
    void (*resume)(void*) noexcept = nullptr;
    bool (*done)(const void*) noexcept = nullptr;
    union {
        wait_result* result = nullptr;
        wait_record<Port>* wait;
    };
};
} // namespace detail
} // namespace coro_os
