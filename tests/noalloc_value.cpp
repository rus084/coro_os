#include <coro/frame_slot>
#include <coro_os/scheduler.hpp>

namespace os = coro_os;
struct value_port {
    std::uint32_t enter_critical() noexcept { return 0; }
    void exit_critical(std::uint32_t) noexcept {}
    void notify_cpu() noexcept {}
    bool in_isr() const noexcept { return false; }
};
using value_scheduler = os::scheduler<value_port>;
struct trace { int alive = 0; int released = 0; int received = 0; int stage = 0; };
struct alignas(32) payload {
    trace* log;
    int value;
    payload() = delete;
    payload(trace& stats, int v) noexcept : log(&stats), value(v) {
        os::detail::require(reinterpret_cast<std::uintptr_t>(this) % alignof(payload) == 0);
        ++log->alive;
    }
    payload(const payload&) = delete;
    payload(payload&& other) noexcept : log(other.log), value(other.value) {
        os::detail::require(reinterpret_cast<std::uintptr_t>(this) % alignof(payload) == 0);
        other.log = nullptr;
    }
    ~payload() { if (log) { --log->alive; ++log->released; } }
};

[[coro::noalloc]] coro::task<int> immediate_int(int value) { co_return value; }
[[coro::noalloc]] coro::task<payload> produce(trace& log) {
    co_await value_scheduler::sleep_for(2);
    co_return payload(log, 7);
}
[[coro::noalloc]] coro::task<int> middle(trace& log) {
    coro::frame<produce> child;
    auto result = co_await child(log);
    os::detail::require(result.value == 7 && log.alive == 1);
    log.stage = 1;
    co_await value_scheduler::wait();
    log.stage = 2;
    co_await value_scheduler::yield();
    co_return result.value + 1;
}
[[coro::noalloc]] coro::task<void> wait_child(trace& log) {
    co_await value_scheduler::wait();
    ++log.stage;
}
[[coro::noalloc]] coro::task<void> value_parent(trace& log) {
    coro::frame_slot<immediate_int, middle, wait_child> children;
    log.received = co_await children.invoke<immediate_int>(40);
    log.received += co_await children.invoke<middle>(log);
    co_await children.invoke<wait_child>(log);
    log.received += co_await children.invoke<immediate_int>(1);
}
[[coro::noalloc]] coro::task<payload> instant_payload(trace& log) { co_return {log, 11}; }
[[coro::noalloc]] coro::task<void> consume_completed(coro::task<payload>& completed, trace& log) {
    auto result = co_await std::move(completed);
    log.received = result.value;
}
static_assert(alignof(coro::frame<produce>) >= alignof(payload));
static_assert(sizeof(coro::task<int>) == sizeof(coro::task<void>));
static_assert(!__is_constructible(payload, const payload&));

int main() {
    coro::frame<value_parent> parent_frame;
    value_scheduler scheduler(value_port{});
    trace log;
    {
        auto root = scheduler.spawn(parent_frame(log));
        os::detail::require(scheduler.poll(0) == 2 && log.received == 40);
        scheduler.poll(2);
        os::detail::require(log.stage == 1 && log.alive == 1 && log.released == 0);
        root.notifier().signal_from_isr(); scheduler.poll(0);
        os::detail::require(log.stage == 2 && log.received == 48 && log.alive == 0 && log.released == 1);
        root.notifier().signal_from_isr(); scheduler.poll(0);
        os::detail::require(log.stage == 3 && log.received == 49);
    }
    for (bool constructed : {false, true}) {
        trace cancelled;
        {
            auto root = scheduler.spawn(parent_frame(cancelled));
            scheduler.poll(0);
            if (constructed) scheduler.poll(2);
        }
        os::detail::require(cancelled.alive == 0 && cancelled.released == (constructed ? 1 : 0));
        os::detail::require(scheduler.poll(100) == os::no_deadline);
    }
    // Destroying a completed, unconsumed result releases exactly its owner.
    coro::frame<instant_payload> result_frame;
    trace unconsumed;
    {
        auto root = scheduler.spawn(result_frame(unconsumed));
        scheduler.poll(0);
        os::detail::require(unconsumed.alive == 1 && unconsumed.released == 0);
    }
    os::detail::require(unconsumed.alive == 0 && unconsumed.released == 1);
    // Completed-child await_ready moves the result before destroying the frame.
    trace completed_log;
    auto completed = result_frame(completed_log);
    os::detail::require(!completed.resume() && completed_log.alive == 1);
    coro::frame<consume_completed> consumer_frame;
    auto consumer = consumer_frame(completed, completed_log);
    os::detail::require(!consumer.resume() && !completed);
    os::detail::require(completed_log.received == 11 && completed_log.alive == 0 && completed_log.released == 1);
    auto reused = result_frame(completed_log);
    os::detail::require(!reused.resume());
    return 0;
}
