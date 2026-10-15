#include <coro_os/scheduler.hpp>
#include "support/test_port.hpp"
#include "support/test_task.hpp"
#include <cstdlib>
#include <new>
#include <type_traits>
#if defined(__unix__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

using namespace coro_os;
extern bool forbid_allocations;

struct sentinel {
    int& count;
    ~sentinel() { ++count; }
};
struct notify_on_destroy {
    test_endpoint& endpoint;
    int& destroyed;
    ~notify_on_destroy() { endpoint.signal(); ++destroyed; }
};
test_task notify_when_cancelled(test_endpoint& endpoint, int& destroyed) {
    notify_on_destroy cleanup{endpoint, destroyed};
    co_await test_scheduler::wait();
}
test_task guarded(int& destroyed, int mode) {
    sentinel guard{destroyed};
    if (mode == 0) co_await test_scheduler::wait();
    if (mode == 1) co_await test_scheduler::sleep_for(9);
    if (mode == 2) co_await test_scheduler::yield();
    if (mode == 3) co_await test_scheduler::wait_for(9);
}
test_task invalid_suspend() { co_await std::suspend_always{}; }
test_task invalid_delay(bool timed) {
    if (timed) co_await test_scheduler::wait_for(no_deadline);
    else co_await test_scheduler::sleep_for(no_deadline);
}
test_task delete_running(void*& job_ptr, void (*destroy)(void*)) {
    destroy(job_ptr); co_return;
}

#if defined(__unix__)
template<class F> void expect_trap(F test) {
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        rlimit limit{0, 0}; setrlimit(RLIMIT_CORE, &limit);
        test(); _exit(0);
    }
    int status = 0; assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP);
}
#endif

int main() {
    test_port p;
    test_scheduler s(p);
    static_assert(std::is_nothrow_move_constructible_v<test_task>);
    for (int mode = 0; mode != 5; ++mode) {
        int destroyed = 0;
        {
            auto j = s.spawn(guarded(destroyed, mode));
            forbid_allocations = true;
            s.poll(0);
            assert(destroyed == (mode == 2 || mode == 4 ? 1 : 0));
            j.notifier().signal_from_isr(); // pending cancellation or completed no-op
        }
        assert(destroyed == 1);
        assert(s.poll(100) == no_deadline && !s.has_work());
        forbid_allocations = false;
    }
    {
        int destroyed = 0;
        test_endpoint endpoint;
        auto notifications = p.signals;
        {
            auto j = s.spawn(notify_when_cancelled(endpoint, destroyed));
            endpoint = j.notifier();
            s.poll(0);
        } // The frame destructor may signal its detached node; this is a no-op.
        assert(destroyed == 1 && p.signals == notifications && !s.has_work());
    }
    {
        int destroyed = 0;
        auto a = s.spawn(guarded(destroyed, 1));
        auto b = s.spawn(guarded(destroyed, 1));
        s.poll(0); a.notifier().signal(); s.poll(9);
        assert(destroyed == 2);
    }
#if defined(__unix__)
    expect_trap([&] {
        int destroyed = 0;
        auto unbound = guarded(destroyed, 0);
        unbound.resume(); // An OS await requires a node bound by spawn.
    });
    expect_trap([&] { p.isr = true; s.poll(0); });
    expect_trap([&] { auto j = s.spawn(invalid_suspend()); s.poll(0); });
    expect_trap([&] {
        test_scheduler other(p);
    });
    for (bool timed : {false, true}) expect_trap([&] {
        auto j = s.spawn(invalid_delay(timed)); s.poll(0);
    });
    expect_trap([&] {
        void* job_ptr = nullptr;
        auto task = delete_running(job_ptr, [](void* ptr) { delete static_cast<job<test_task, test_port&>*>(ptr); });
        auto* j = new job<test_task, test_port&>(s.spawn(std::move(task)));
        job_ptr = j; s.poll(0);
    });
    expect_trap([&] { test_endpoint{}.signal(); });
#endif
}
