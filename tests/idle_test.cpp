#include "support/idle_port.hpp"
#include "support/test_task.hpp"

using idle_scheduler = coro_os::scheduler<idle_port&>;
using idle_endpoint = coro_os::notification_endpoint<idle_port&>;

test_task waiting(int& count) {
    co_await idle_scheduler::wait();
    ++count;
}
void signal_irq(idle_port& port) {
    auto& endpoint = *static_cast<idle_endpoint*>(port.payload);
    port.isr = true;
    endpoint.signal_from_isr();
    port.isr = false;
}

int main() {
    // Test the scheduler/idle-port contract, not ARM instructions on the host.
    for (int point = 0; point != 3; ++point) {
        idle_port port;
        idle_scheduler scheduler(port);
        int count = 0;
        auto job = scheduler.spawn(waiting(count));
        auto endpoint = job.notifier();
        port.payload = &endpoint;
        scheduler.poll(0);
        if (point == 0) signal_irq(port); // Before the readiness check.
        if (point == 1) port.before_sleep = signal_irq;
        if (point == 2) port.while_sleeping = signal_irq;
        port.idle_if([&]() noexcept { return scheduler.has_work(); });
        assert(!port.sleeping && count == 0 && scheduler.has_work());
        scheduler.poll(0);
        assert(count == 1);
    }
    idle_port port;
    port.notify_cpu(); // A notification without work may cause a spurious return.
    port.idle_if([]() noexcept { return false; });
    assert(!port.sleeping && !port.event);
    port.idle_if([]() noexcept { return false; });
    assert(port.sleeping);
}
