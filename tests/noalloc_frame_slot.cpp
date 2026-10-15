#include <coro/frame_slot>
#include <coro_os/scheduler.hpp>
#if defined(__unix__)
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace os = coro_os;
struct slot_port {
    std::uint32_t enter_critical() noexcept { return 0; }
    void exit_critical(std::uint32_t) noexcept {}
    void notify_cpu() noexcept {}
    bool in_isr() const noexcept { return false; }
};
using slot_scheduler = os::scheduler<slot_port>;
struct trace { unsigned progress = 0; unsigned destroyed = 0; };
struct guard { trace& log; ~guard() { ++log.destroyed; } };

[[coro::noalloc]] coro::task<void> large_child(trace& log, unsigned value) {
    guard cleanup{log};
    alignas(32) volatile unsigned char data[128]{};
    data[value & 127] = static_cast<unsigned char>(value);
    co_await slot_scheduler::sleep_for(2);
    log.progress += data[value & 127];
}
[[coro::noalloc]] coro::task<void> small_child(trace& log) {
    guard cleanup{log};
    co_await slot_scheduler::wait();
    ++log.progress;
}
[[coro::noalloc]] coro::task<void> instant_child(trace& log) {
    ++log.progress;
    co_return;
}
[[coro::noalloc]] coro::task<void> unlisted_child(trace&) { co_return; }
using slot = coro::frame_slot<large_child, small_child, instant_child>;
using reordered_slot = coro::frame_slot<instant_child, small_child, large_child>;
template<class Storage, auto Function>
concept accepts_function = requires(Storage& storage, trace& log) {
    storage.template invoke<Function>(log);
};
static_assert(accepts_function<slot, small_child>);
static_assert(!accepts_function<slot, unlisted_child>);
static_assert(reordered_slot::capacity == slot::capacity);
static_assert(reordered_slot::alignment == slot::alignment);
static_assert(sizeof(coro::frame_slot<small_child>) == sizeof(coro::frame<small_child>));
static_assert(slot::capacity == __builtin_coro_frame_size(large_child));
static_assert(slot::alignment == __builtin_coro_frame_align(large_child));
static_assert(slot::alignment >= 32);
static_assert(alignof(slot) == slot::alignment);
static_assert(sizeof(slot) == ((slot::capacity + 1 + slot::alignment - 1) / slot::alignment) * slot::alignment);
static_assert(sizeof(slot) < sizeof(coro::frame<large_child>) + sizeof(coro::frame<small_child>));
static_assert(!__is_constructible(slot, slot&&));
static_assert(!__is_constructible(slot, const slot&));
constinit slot static_slot;

[[coro::noalloc]] coro::task<void> slot_parent(trace& log) {
    reordered_slot children;
    for (unsigned i = 0; i != 2; ++i) {
        co_await children.invoke<large_child>(log, 3u);
        co_await children.invoke<small_child>(log);
        co_await children.invoke<instant_child>(log);
    }
}

#if defined(__unix__)
template<class F> void expect_trap(F test) {
    auto pid = fork(); os::detail::require(pid >= 0);
    if (pid == 0) {
        rlimit limit{0, 0}; setrlimit(RLIMIT_CORE, &limit);
        test(); _exit(0);
    }
    int status = 0;
    os::detail::require(waitpid(pid, &status, 0) == pid);
    os::detail::require(WIFSIGNALED(status));
    os::detail::require(WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP);
}
#endif

int main() {
    coro::frame<slot_parent> parent_frame;
    slot_scheduler scheduler(slot_port{});
    trace log;
    {
        auto root = scheduler.spawn(parent_frame(log));
        for (unsigned i = 0; i != 2; ++i) {
            os::detail::require(scheduler.poll(0) == 2);
            os::detail::require(scheduler.poll(2) == os::no_deadline);
            os::detail::require(log.progress == 3 + i * 5);
            root.notifier().signal_from_isr();
            os::detail::require(scheduler.poll(0) == (i == 0 ? 2 : os::no_deadline));
        }
    }
    os::detail::require(log.progress == 10 && log.destroyed == 4);
    for (bool waiting : {false, true}) {
        trace cancelled;
        {
            auto root = scheduler.spawn(parent_frame(cancelled));
            scheduler.poll(0);
            if (waiting) scheduler.poll(2);
            root.notifier().signal_from_isr();
        }
        os::detail::require(cancelled.destroyed == (waiting ? 2u : 1u));
        os::detail::require(scheduler.poll(100) == os::no_deadline);
    }
    // Destroying an unstarted child releases the same slot for another function.
    { auto child = static_slot.invoke<large_child>(log, 7u); }
    { auto child = static_slot.invoke<instant_child>(log); os::detail::require(!child.resume()); }
#if defined(__unix__)
    expect_trap([&] {
        slot children;
        auto first = children.invoke<large_child>(log, 1u);
        auto second = children.invoke<small_child>(log);
    });
    expect_trap([&] {
        slot children;
        auto finished = children.invoke<instant_child>(log);
        finished.resume(); // Final suspension still owns the slot.
        auto second = children.invoke<small_child>(log);
    });
    expect_trap([&] {
        coro::task<void> live;
        { slot children; live = children.invoke<small_child>(log); }
    });
#endif
    return 0;
}
