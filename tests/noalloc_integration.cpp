#include <coro/frame>
#include <coro/task>
#include <coro_os/scheduler.hpp>

namespace os = coro_os;
struct integration_port {
    unsigned depth = 0;
    std::uint32_t enter_critical() noexcept { return depth++; }
    void exit_critical(std::uint32_t saved) noexcept { depth = saved; }
    void notify_cpu() noexcept {}
    bool in_isr() const noexcept { return false; }
};
using integration_scheduler = os::scheduler<integration_port>;
struct guard {
    int& count;
    ~guard() { ++count; }
};
[[coro::noalloc]] coro::task<void> operation(int& stage, int& destroyed) {
    guard cleanup{destroyed};
    co_await integration_scheduler::sleep_for(3);
    ++stage;
    co_await integration_scheduler::wait();
    ++stage;
}
int main() {
    // Storage first, then scheduler, then job: reverse destruction is safe.
    coro::frame<operation> frame;
    integration_scheduler s(integration_port{});
    int stage = 0, destroyed = 0;
    {
        auto task = frame(stage, destroyed);
        os::detail::require(stage == 0 && destroyed == 0 && !task.done());
        auto job = s.spawn(std::move(task));
        os::detail::require(s.poll(0) == 3);
        s.poll(3); os::detail::require(stage == 1);
        job.notifier().signal_from_isr();
        s.poll(0); os::detail::require(stage == 2 && destroyed == 1);
    }
    // Completed frame must be freed by destroy, then reusable without allocating.
    {
        auto job = s.spawn(frame(stage, destroyed));
        s.poll(0);
    } // cancel while sleeping
    os::detail::require(destroyed == 2 && s.poll(100) == os::no_deadline);
    return 0;
}
