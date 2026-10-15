#include <coro_os/scheduler.hpp>
#include "support/test_task.hpp"
#include <cassert>
#include <type_traits>

struct empty_port {
    static std::uint32_t enter_critical() noexcept { return 0; }
    static void exit_critical(std::uint32_t) noexcept {}
    static void notify_cpu() noexcept {}
    static bool in_isr() noexcept { return false; }
};
struct stateful_port {
    unsigned depth = 0;
    unsigned notifications = 0;
    std::uint32_t enter_critical() noexcept { return depth++; }
    void exit_critical(std::uint32_t saved) noexcept { assert(depth == saved + 1); depth = saved; }
    void notify_cpu() noexcept { ++notifications; }
    bool in_isr() const noexcept { return false; }
};
struct copy_only_empty_port : empty_port {
    copy_only_empty_port() noexcept = default;
    copy_only_empty_port(const copy_only_empty_port&) noexcept = default;
    copy_only_empty_port(copy_only_empty_port&&) = delete;
};
struct move_only_port : stateful_port {
    move_only_port() noexcept = default;
    move_only_port(const move_only_port&) = delete;
    move_only_port(move_only_port&&) noexcept = default;
};
template<class Scheduler> test_task waiter(int& count) {
    co_await Scheduler::wait(); ++count;
}
int main() {
    using empty_scheduler = coro_os::scheduler<empty_port>;
    using stateful_scheduler = coro_os::scheduler<stateful_port&>;
    static_assert(std::is_empty_v<empty_port>);
    static_assert(sizeof(empty_scheduler) == 5 * sizeof(void*));
    static_assert(sizeof(empty_scheduler) < sizeof(stateful_scheduler));
    static_assert(!std::is_convertible_v<coro_os::notification_endpoint<empty_port>,
                                        coro_os::notification_endpoint<stateful_port&>>);
    {
        empty_scheduler a(empty_port{});
        int count = 0;
        auto job = a.spawn(waiter<decltype(a)>(count));
        a.poll(0); job.notifier().signal_from_isr(); a.poll(0);
        assert(count == 1);
    }
    {
        empty_port empty;
        coro_os::scheduler inferred(empty);
        static_assert(std::is_same_v<decltype(inferred), empty_scheduler>);
    }
    {
        copy_only_empty_port copy_only;
        coro_os::scheduler borrowed(copy_only);
        static_assert(std::is_same_v<decltype(borrowed), coro_os::scheduler<copy_only_empty_port&>>);
    }
    {
        move_only_port move_only;
        coro_os::scheduler noncopyable_borrow(move_only);
        static_assert(std::is_same_v<decltype(noncopyable_borrow), coro_os::scheduler<move_only_port&>>);
    }
    {
        move_only_port move_only;
        coro_os::scheduler owned(std::move(move_only));
        static_assert(std::is_same_v<decltype(owned), coro_os::scheduler<move_only_port>>);
        int count = 0;
        auto job = owned.spawn(waiter<decltype(owned)>(count));
        owned.poll(0); job.notifier().signal(); owned.poll(0);
        assert(count == 1);
    }
    {
        stateful_port port;
        coro_os::scheduler b(port);
        static_assert(std::is_same_v<decltype(b), stateful_scheduler>);
        int count = 0;
        auto job = b.spawn(waiter<decltype(b)>(count));
        b.poll(0); job.notifier().signal_from_isr(); b.poll(0);
        assert(count == 1 && port.notifications == 1 && port.depth == 0);
    }
}
