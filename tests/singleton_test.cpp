#include <coro_os/scheduler.hpp>
#include "support/test_port.hpp"
#include "support/test_task.hpp"
#include <cassert>
#if defined(__unix__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

struct other_port {
    static std::uint32_t enter_critical() noexcept { return 0; }
    static void exit_critical(std::uint32_t) noexcept {}
    static void notify_cpu() noexcept {}
    static bool in_isr() noexcept { return false; }
};
void create_scheduler_in_other_tu();
test_task without_scheduler_argument(int& count) {
    co_await test_scheduler::sleep_for(2);
    ++count;
    co_await test_scheduler::wait();
    ++count;
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
    test_port port;
    for (int n = 0; n != 2; ++n) {
        test_scheduler scheduler(port);
        int count = 0;
        auto job = scheduler.spawn(without_scheduler_argument(count));
        assert(scheduler.poll(0) == 2);
        scheduler.poll(2); assert(count == 1);
        job.notifier().signal_from_isr(); scheduler.poll(0);
        assert(count == 2);
#if defined(__unix__)
        expect_trap([&] { test_scheduler second(port); });
        expect_trap([] { coro_os::scheduler second(other_port{}); });
        expect_trap([] { create_scheduler_in_other_tu(); });
#endif
    }
    create_scheduler_in_other_tu(); // Sequential reuse after destruction is permitted.
#if defined(__unix__)
    expect_trap([&] {
        int count = 0;
        auto task = without_scheduler_argument(count);
        task.resume(); // await without an active scheduler must fail explicitly.
    });
#endif
}
