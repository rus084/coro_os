// Integration skeleton: board startup configures a periodic SysTick and IRQs.
#include "board.hpp"

static volatile coro_os::tick_t ticks = 0;
extern "C" void SysTick_Handler() {
    ticks = ticks + 1;
    board_port::notify_cpu();
}

[[noreturn]] void run_scheduler(board_scheduler& scheduler) {
    auto last = ticks;
    for (;;) {
        const auto sampled = ticks;
        const auto elapsed = sampled - last;
        last = sampled;
        scheduler.poll(elapsed);
        // Checking ticks also preserves an IRQ that happened before idle cleared
        // the old CPU event; timer time is accounted at the next poll.
        coro_os::ports::cortex_m::idle_if([&]() noexcept {
            return scheduler.has_work() || ticks != sampled;
        });
    }
}
