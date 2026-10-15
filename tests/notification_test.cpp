#include <coro_os/scheduler.hpp>
#include "support/test_port.hpp"
#include "support/test_task.hpp"

using namespace coro_os;
test_task wait_twice(int& count) {
    co_await test_scheduler::wait(); ++count;
    co_await test_scheduler::wait(); ++count;
}
test_task timed_then_wait(tick_t delay, wait_result& result, int& count) {
    result = co_await test_scheduler::wait_for(delay); ++count;
    co_await test_scheduler::wait(); ++count;
}
test_task sleep_then_wait(int& count) {
    co_await test_scheduler::sleep_for(5); ++count;
    co_await test_scheduler::wait(); ++count;
}
void inject_signal(test_port& p) {
    auto& ep = *static_cast<test_endpoint*>(p.payload);
    bool saved = p.isr; p.isr = true; ep.signal_from_isr(); p.isr = saved;
}
test_task boundary_wait(test_port& p, int& count, bool before) {
    if (before) p.on_enter = inject_signal; else p.on_exit = inject_signal;
    co_await test_scheduler::wait(); ++count;
}
test_task producer(test_endpoint& ep) {
    ep.signal(); co_await test_scheduler::yield(); ep.signal();
}
test_task signal_while_running(test_endpoint& ep, int& count) {
    ep.signal(); co_await test_scheduler::wait(); ++count;
}
struct injection {
    test_endpoint endpoint;
    unsigned boundary = 0;
    unsigned target = 0;
    bool flood = false;
};
void pending_injection(test_port& p) {
    auto& ctx = *static_cast<injection*>(p.payload);
    ++ctx.boundary;
    if (ctx.flood || ctx.boundary == ctx.target) ctx.endpoint.signal_from_isr();
    if (ctx.boundary < (ctx.flood ? 1000u : ctx.target)) p.on_exit = pending_injection;
}

int main() {
    test_port p;
    test_scheduler s(p);
    int count = 0;
    {
        auto j = s.spawn(wait_twice(count));
        auto ep = j.notifier();
        ep.signal(); ep.signal();
        assert(p.signals == 2);
        assert(s.has_work());
        s.poll(0); assert(count == 1);
        p.isr = true; ep.signal_from_isr(); p.isr = false;
        assert(count == 1);
        s.poll(0); assert(count == 2);
        ep.signal(); s.poll(0); assert(count == 2);
    }
    count = 0;
    {
        auto j = s.spawn(sleep_then_wait(count));
        auto ep = j.notifier(); s.poll(0);
        ep.signal(); assert(s.poll(0) == 5 && count == 0);
        s.poll(5); assert(count == 2);
    }
    for (bool before : {false, true}) {
        count = 0;
        auto j = s.spawn(boundary_wait(p, count, before));
        auto ep = j.notifier(); p.payload = &ep;
        for (int n = 0; n != 3; ++n) s.poll(0);
        assert(count == 1);
    }
    for (bool token : {false, true}) {
        count = 0; wait_result result{};
        auto j = s.spawn(timed_then_wait(0, result, count));
        if (token) j.notifier().signal();
        s.poll(0);
        assert(count == 1 && result == (token ? wait_result::notified : wait_result::timeout));
    }
    for (bool before : {false, true}) {
        count = 0; wait_result result{};
        auto j = s.spawn(timed_then_wait(5, result, count));
        auto ep = j.notifier(); p.payload = &ep;
        s.poll(0);
        if (before) p.on_enter = inject_signal; else p.on_exit = inject_signal;
        s.poll(5);
        assert(result == (before ? wait_result::notified : wait_result::timeout));
        assert(count == (before ? 1 : 2));
    }
    count = 0;
    {
        auto consumer = s.spawn(wait_twice(count));
        auto ep = consumer.notifier();
        auto sender = s.spawn(producer(ep));
        s.poll(0); assert(count == 2);
    }
    count = 0;
    {
        test_endpoint ep;
        auto j = s.spawn(signal_while_running(ep, count)); ep = j.notifier();
        s.poll(0); assert(count == 1);
    }
    // Cancellation of each position of the timer queue preserves the others.
    for (int notified = 0; notified != 3; ++notified) {
        int counts[3]{}; wait_result results[3]{};
        auto a = s.spawn(timed_then_wait(3, results[0], counts[0]));
        auto b = s.spawn(timed_then_wait(7, results[1], counts[1]));
        auto c = s.spawn(timed_then_wait(10, results[2], counts[2]));
        test_endpoint ep[] = {a.notifier(), b.notifier(), c.notifier()};
        s.poll(0); ep[notified].signal(); s.poll(0);
        assert(counts[notified] == 1 && results[notified] == wait_result::notified);
        s.poll(3); if (notified != 0) assert(counts[0] == 1);
        s.poll(4); if (notified != 1) assert(counts[1] == 1);
        s.poll(3); if (notified != 2) assert(counts[2] == 1);
        for (int i = 0; i != 3; ++i) if (i != notified) assert(results[i] == wait_result::timeout);
    }
    count = 0;
    {
        auto j = s.spawn(wait_twice(count));
        auto ep = j.notifier();
        s.poll(0); ep.signal();
        // Boundary 1 is snapshot exit, boundary 2 is pop/transition exit.
        // A second token after the transition must satisfy the second wait.
        injection ctx{ep, 0, 2, false}; p.payload = &ctx;
        p.on_exit = pending_injection;
        s.poll(0); assert(count == 2 && ctx.boundary == 2);
    }
    count = 0;
    {
        auto j = s.spawn(wait_twice(count));
        auto ep = j.notifier(); s.poll(0); ep.signal();
        injection ctx{ep, 0, 0, true}; p.payload = &ctx;
        p.on_exit = pending_injection;
        s.poll(0);
        assert(count == 2 && ctx.boundary < 100); // Snapshot draining lets the task finish despite repeated IRQ signals.
        p.on_exit = nullptr;
        s.poll(0); assert(count == 2);
    }
}
