#include "board.hpp"
#include <coro/frame>
#include <coro_os/scheduler.hpp>
#include <coro_os/ports/cortex_m.hpp>
using port = coro_os::ports::cortex_m::port;
using scheduler = coro_os::scheduler<port>;
static coro_os::notification_endpoint<port> button_endpoint;

[[coro::noalloc]] coro::task<void> blink(unsigned led) {
    for (;;) {
        co_await scheduler::sleep_for(nucleo::half_period(led, nucleo::fast()));
        nucleo::led_toggle(led);
    }
}
[[coro::noalloc]] coro::task<void> button_worker() {
    using namespace nucleo;
    if (button_pressed()) button_endpoint.signal();
    for (;;) {
        co_await scheduler::wait();
        std::uint8_t pressed = 0, released = 0;
        while (pressed < 30 && released < 30) {
            co_await scheduler::sleep_for(1);
            if (button_pressed()) { ++pressed; released = 0; }
            else { ++released; pressed = 0; }
        }
        if (released == 30) continue;
        accept_press();
        for (released = 0; released < 30;) {
            co_await scheduler::sleep_for(1);
            released = button_pressed() ? 0 : released + 1;
        }
    }
}
static coro::frame<blink> led_frames[3];
static coro::frame<button_worker> button_frame;
namespace nucleo {
void button_irq() noexcept { button_endpoint.signal_from_isr(); }
}
extern "C" void SysTick_Handler() { nucleo::tick_irq(); }
extern "C" int main() {
    using namespace nucleo;
    init(); debug_state.variant = 3;
    scheduler os(port{});
    auto led0 = os.spawn(led_frames[0](0));
    auto led1 = os.spawn(led_frames[1](1));
    auto led2 = os.spawn(led_frames[2](2));
    auto button = os.spawn(button_frame());
    button_endpoint = button.notifier();
    enable_button_irq();
    start_ticks();
    auto last = ticks();
    for (;;) {
        const auto now = ticks();
        os.poll(now - last); last = now;
        coro_os::ports::cortex_m::idle_if([&]() noexcept {
            return os.has_work() || ticks() != now;
        });
    }
}
