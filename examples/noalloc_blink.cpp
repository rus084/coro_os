// Compile with the new noalloc Clang. Board code provides run_scheduler and IRQ setup.
#include <coro/frame>
#include <coro/task>
#include "board.hpp"

static volatile std::uint32_t led_state = 0;
static volatile std::uint32_t work_count = 0;
static board_endpoint hardware_endpoint;

[[coro::noalloc]] coro::task<void> blink_once() {
    led_state = led_state ^ 1u;
    co_await board_scheduler::sleep_for(500); // Board chooses tick duration.
}
[[coro::noalloc]] coro::task<void> blink() {
    // This storage lives inside blink's coroutine frame, across suspension.
    coro::frame<blink_once> child_frame;
    for (;;) {
        co_await child_frame();
        // The child is destroyed before its frame is reused. Both share one job.
    }
}
[[coro::noalloc]] coro::task<void> worker() {
    for (;;) {
        co_await board_scheduler::wait();
        work_count = work_count + 1;
    }
}
[[coro::noalloc]] coro::task<void> irq_worker(board_endpoint& peer) {
    for (;;) {
        co_await board_scheduler::wait();
        // Drain board-owned RX/DMA data here before signalling the peer.
        peer.signal();
    }
}
static coro::frame<blink> blink_frame;
static coro::frame<worker> worker_frame;
static coro::frame<irq_worker> irq_frame;

extern "C" void Peripheral_IRQHandler() {
    // Board must acknowledge the hardware interrupt source.
    hardware_endpoint.signal_from_isr();
}
[[noreturn]] void run_application() {
    board_scheduler scheduler(board_port{});
    auto background = scheduler.spawn(worker_frame());
    auto peer = background.notifier();
    auto receiver = scheduler.spawn(irq_frame(peer));
    auto blinker = scheduler.spawn(blink_frame());
    hardware_endpoint = receiver.notifier();
    configure_board_irqs();
    run_scheduler(scheduler);
}
