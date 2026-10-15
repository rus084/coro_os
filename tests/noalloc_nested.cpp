#include <coro/frame>
#include <coro_os/scheduler.hpp>

namespace os = coro_os;
struct nested_port {
    unsigned depth = 0;
    std::uint32_t enter_critical() noexcept { return depth++; }
    void exit_critical(std::uint32_t saved) noexcept { depth = saved; }
    void notify_cpu() noexcept {}
    bool in_isr() const noexcept { return false; }
};
using nested_scheduler = os::scheduler<nested_port>;
struct trace {
    void* context = nullptr;
    unsigned started = 0;
    unsigned count = 0;
    int destroyed[8]{};
    unsigned completed = 0;
    os::wait_result result = os::wait_result::timeout;
};
struct context_probe {
    trace& log;
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        auto context = handle.promise().context();
        os::detail::require(context != nullptr);
        if (!log.context) log.context = context;
        os::detail::require(log.context == context);
        return false;
    }
    void await_resume() const noexcept {}
};
struct guard {
    trace& log;
    int id;
    ~guard() { log.destroyed[log.count++] = id; }
};

[[coro::noalloc]] coro::task<void> nested_leaf(trace& log) {
    co_await context_probe{log};
    guard cleanup{log, 3};
    ++log.started;
    co_await nested_scheduler::sleep_for(3);
    log.result = co_await nested_scheduler::wait_for(5);
    co_await nested_scheduler::yield();
    ++log.completed;
}
[[coro::noalloc]] coro::task<void> nested_middle(trace& log) {
    co_await context_probe{log};
    guard cleanup{log, 2};
    ++log.started;
    coro::frame<nested_leaf> child_frame;
    co_await child_frame(log);
    ++log.completed;
}
[[coro::noalloc]] coro::task<void> nested_parent(trace& log) {
    co_await context_probe{log};
    guard cleanup{log, 1};
    ++log.started;
    coro::frame<nested_middle> child_frame;
    // Reuse the same nested storage after the owning awaiter destroys the child.
    for (unsigned i = 0; i != 2; ++i)
        co_await child_frame(log);
    ++log.completed;
}

[[coro::noalloc]] coro::task<void> waiting_leaf(trace& log) {
    guard cleanup{log, 3};
    co_await nested_scheduler::wait();
    ++log.completed;
}
[[coro::noalloc]] coro::task<void> waiting_parent(trace& log) {
    guard cleanup{log, 1};
    coro::frame<waiting_leaf> child_frame;
    co_await child_frame(log);
    ++log.completed;
}
[[coro::noalloc]] coro::task<void> immediate_child(unsigned& count) {
    ++count;
    co_return;
}
[[coro::noalloc]] coro::task<void> immediate_parent(unsigned& count) {
    coro::frame<immediate_child> child_frame;
    for (unsigned i = 0; i != 128; ++i)
        co_await child_frame(count);
}
[[coro::noalloc]] coro::task<void> manual_leaf(unsigned& count) {
    co_await std::suspend_always{};
    ++count;
}
[[coro::noalloc]] coro::task<void> manual_parent(unsigned& count) {
    coro::frame<manual_leaf> child_frame;
    co_await child_frame(count);
    ++count;
}
[[coro::noalloc]] coro::task<void> join_completed(coro::task<void>& child, unsigned& count) {
    co_await std::move(child);
    ++count;
}

int main() {
    coro::frame<nested_parent> frame;
    nested_scheduler scheduler(nested_port{});
    for (bool notify : {false, true}) {
        trace log;
        {
            auto root = scheduler.spawn(frame(log));
            for (unsigned i = 0; i != 2; ++i) {
                os::detail::require(scheduler.poll(0) == 3);
                os::detail::require(log.started == 3 + i * 2);
                os::detail::require(scheduler.poll(3) == 5);
                if (notify) root.notifier().signal_from_isr();
                auto delay = scheduler.poll(notify ? 0 : 5);
                os::detail::require(delay == (i == 0 ? 3 : os::no_deadline));
                os::detail::require(log.result == (notify ? os::wait_result::notified : os::wait_result::timeout));
                os::detail::require(log.completed == (i == 0 ? 2u : 5u));
            }
        }
        os::detail::require(log.count == 5);
        const int expected[] = {3, 2, 3, 2, 1};
        for (unsigned i = 0; i != 5; ++i)
            os::detail::require(log.destroyed[i] == expected[i]);
    }
    // Cancel at the deepest sleep and timed wait, including a pending IRQ token.
    for (bool waiting : {false, true}) {
        trace log;
        {
            auto root = scheduler.spawn(frame(log));
            scheduler.poll(0);
            if (waiting) scheduler.poll(3);
            root.notifier().signal_from_isr();
        }
        os::detail::require(log.completed == 0 && log.count == 3);
        for (unsigned i = 0; i != 3; ++i)
            os::detail::require(log.destroyed[i] == 3 - static_cast<int>(i));
        os::detail::require(scheduler.poll(100) == os::no_deadline);
    }
    coro::frame<waiting_parent> waiting_frame;
    for (bool cancel : {false, true}) {
        trace log;
        {
            auto root = scheduler.spawn(waiting_frame(log));
            os::detail::require(scheduler.poll(0) == os::no_deadline);
            root.notifier().signal_from_isr();
            os::detail::require(log.completed == 0);
            if (!cancel) scheduler.poll(0);
        }
        os::detail::require(log.count == 2 && log.destroyed[0] == 3 && log.destroyed[1] == 1);
        os::detail::require(log.completed == (cancel ? 0u : 2u));
        os::detail::require(scheduler.poll(0) == os::no_deadline);
    }
    // Synchronous children also work without a scheduler-owned task.
    coro::frame<immediate_parent> immediate_frame;
    unsigned count = 0;
    auto immediate = immediate_frame(count);
    os::detail::require(!immediate.resume() && immediate.done() && count == 128);
    // Moving the root descriptor does not invalidate the promise/active-leaf links.
    coro::frame<manual_parent> manual_frame;
    auto manual = manual_frame(count);
    os::detail::require(manual.resume() && count == 128);
    auto moved = std::move(manual);
    os::detail::require(!manual && !moved.resume() && count == 130);
    // A completed child's await_ready path also releases its storage.
    coro::frame<immediate_child> completed_frame;
    auto completed = completed_frame(count);
    os::detail::require(!completed.resume() && count == 131);
    coro::frame<join_completed> joined_frame;
    auto joined = joined_frame(completed, count);
    os::detail::require(!joined.resume() && !completed && count == 132);
    auto reused = completed_frame(count);
    os::detail::require(!reused.resume() && count == 133);
    return 0;
}
