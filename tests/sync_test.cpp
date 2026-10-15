#include <coro_os/mutex.hpp>
#include <coro_os/semaphore.hpp>
#include "support/test_port.hpp"
#include "support/test_task.hpp"
#include <array>

using test_mutex = coro_os::mutex<test_port&>;
using test_semaphore = coro_os::semaphore<test_port&, 3>;
struct trace {
    std::array<int, 32> data{};
    unsigned size = 0;
    void add(int value) { assert(size < data.size()); data[size++] = value; }
};
test_task locked(test_mutex& mutex, trace& log, int id, bool hold) {
    auto guard = co_await mutex.lock();
    log.add(id);
    if (hold) co_await test_scheduler::wait();
}
test_task acquire(test_semaphore& semaphore, trace& log, int id) {
    co_await semaphore.acquire(); log.add(id);
}
test_task timed(test_semaphore& semaphore, coro_os::tick_t delay, bool& result, int& resumed) {
    result = co_await semaphore.acquire_for(delay); ++resumed;
}
test_task acquire_then_notification(test_semaphore& semaphore, int& resumed) {
    co_await semaphore.acquire(); ++resumed;
    co_await test_scheduler::wait(); ++resumed;
}
void release_irq(test_port& port) {
    auto& semaphore = *static_cast<test_semaphore*>(port.payload);
    bool saved = port.isr; port.isr = true;
    assert(semaphore.release_from_isr());
    port.isr = saved;
}
test_task registration_race(test_port& port, test_semaphore& semaphore, bool& result) {
    port.on_exit = release_irq;
    result = co_await semaphore.acquire_for(5);
}

int main() {
    test_port port;
    test_scheduler scheduler(port);
    {
        test_mutex mutex;
        trace log;
        auto owner = scheduler.spawn(locked(mutex, log, 1, true));
        auto second = scheduler.spawn(locked(mutex, log, 2, false));
        auto third = scheduler.spawn(locked(mutex, log, 3, false));
        scheduler.poll(0); assert(log.size == 1);
        second.notifier().signal_from_isr(); scheduler.poll(0);
        assert(log.size == 1); // A notification does not acquire the mutex.
        owner.notifier().signal(); scheduler.poll(0);
        assert(log.size == 3 && log.data[1] == 2 && log.data[2] == 3);
    }
    {
        test_mutex mutex;
        trace log;
        {
            auto owner = scheduler.spawn(locked(mutex, log, 1, true));
            scheduler.poll(0);
            {
                auto cancelled = scheduler.spawn(locked(mutex, log, 2, false));
                scheduler.poll(0);
            }
        } // Cancellation releases an acquired RAII guard.
        auto next = scheduler.spawn(locked(mutex, log, 3, false));
        scheduler.poll(0);
        assert(log.size == 2 && log.data[1] == 3);
    }
    {
        test_semaphore semaphore;
        trace log;
        auto first = scheduler.spawn(acquire(semaphore, log, 1));
        auto second = scheduler.spawn(acquire(semaphore, log, 2));
        auto third = scheduler.spawn(acquire(semaphore, log, 3));
        scheduler.poll(0);
        first.notifier().signal(); scheduler.poll(0); assert(log.size == 0);
        assert(semaphore.release_from_isr() && semaphore.release_from_isr());
        assert(log.size == 0);
        scheduler.poll(0); assert(log.size == 2 && log.data[0] == 1 && log.data[1] == 2);
        assert(semaphore.release()); scheduler.poll(0); assert(log.data[2] == 3);
    }
    for (bool before : {false, true}) {
        test_semaphore semaphore;
        bool result = false; int resumed = 0;
        auto waiter = scheduler.spawn(timed(semaphore, 5, result, resumed));
        assert(scheduler.poll(0) == 5);
        port.payload = &semaphore;
        if (before) port.on_enter = release_irq; else port.on_exit = release_irq;
        scheduler.poll(5);
        assert(resumed == 1 && result == before);
        bool immediate = false; int count = 0;
        auto check = scheduler.spawn(timed(semaphore, 0, immediate, count));
        scheduler.poll(0);
        assert(count == 1 && immediate == !before);
    }
    {
        test_semaphore semaphore{3};
        assert(!semaphore.release() && !semaphore.release_from_isr());
        bool result = false; int resumed = 0;
        auto waiter = scheduler.spawn(timed(semaphore, 0, result, resumed));
        scheduler.poll(0); assert(result && resumed == 1);
        assert(semaphore.release());
    }
    {
        test_mutex mutex;
        trace log;
        using job_type = coro_os::job<test_task, test_port&>;
        auto* owner = new job_type(scheduler.spawn(locked(mutex, log, 1, true)));
        auto* cancelled = new job_type(scheduler.spawn(locked(mutex, log, 2, false)));
        auto next = scheduler.spawn(locked(mutex, log, 3, false));
        scheduler.poll(0);
        delete owner; // Hands the lock to cancelled, without resuming it.
        delete cancelled; // Must pass the unaccepted grant to next.
        assert(log.size == 1);
        scheduler.poll(0);
        assert(log.size == 2 && log.data[1] == 3);
    }
    {
        test_semaphore semaphore;
        int resumed = 0;
        auto waiter = scheduler.spawn(acquire_then_notification(semaphore, resumed));
        scheduler.poll(0);
        waiter.notifier().signal(); scheduler.poll(0); assert(resumed == 0);
        assert(semaphore.release()); scheduler.poll(0); assert(resumed == 2);
    }
    {
        test_semaphore semaphore;
        bool result = false;
        port.payload = &semaphore;
        auto waiter = scheduler.spawn(registration_race(port, semaphore, result));
        assert(scheduler.poll(0) == coro_os::no_deadline && result);
    }
    {
        test_semaphore semaphore;
        bool result = false; int resumed = 0;
        {
            auto waiter = scheduler.spawn(timed(semaphore, 5, result, resumed));
            scheduler.poll(0);
            port.payload = &semaphore;
            port.on_exit = release_irq; // After detach, before awaiter destruction.
        }
        assert(resumed == 0);
        auto check = scheduler.spawn(timed(semaphore, 0, result, resumed));
        scheduler.poll(0); assert(result && resumed == 1);
    }
    for (int cancelled = 0; cancelled != 3; ++cancelled) {
        test_semaphore semaphore;
        bool results[3]{}; int resumed[3]{};
        using job_type = coro_os::job<test_task, test_port&>;
        job_type* jobs[3];
        for (int i = 0; i != 3; ++i)
            jobs[i] = new job_type(scheduler.spawn(timed(semaphore, 3 + i * 4, results[i], resumed[i])));
        scheduler.poll(0);
        delete jobs[cancelled]; jobs[cancelled] = nullptr;
        assert(semaphore.release()); scheduler.poll(0); scheduler.poll(20);
        int first = cancelled == 0 ? 1 : 0;
        for (int i = 0; i != 3; ++i) {
            assert(resumed[i] == (i == cancelled ? 0 : 1));
            assert(results[i] == (i == first));
            delete jobs[i];
        }
    }
    {
        test_semaphore semaphore;
        trace log;
        using job_type = coro_os::job<test_task, test_port&>;
        auto* cancelled = new job_type(scheduler.spawn(acquire(semaphore, log, 1)));
        auto next = scheduler.spawn(acquire(semaphore, log, 2));
        scheduler.poll(0);
        assert(semaphore.release()); delete cancelled;
        scheduler.poll(0); assert(log.size == 1 && log.data[0] == 2);
    }
    {
        // All three issued grants are cancelled before their tasks resume.
        test_semaphore semaphore;
        trace cancelled;
        {
            auto a = scheduler.spawn(acquire(semaphore, cancelled, 1));
            auto b = scheduler.spawn(acquire(semaphore, cancelled, 2));
            auto c = scheduler.spawn(acquire(semaphore, cancelled, 3));
            scheduler.poll(0);
            assert(semaphore.release() && semaphore.release() && semaphore.release());
            assert(!semaphore.release());
        }
        assert(cancelled.size == 0);
        trace log;
        auto a = scheduler.spawn(acquire(semaphore, log, 1));
        auto b = scheduler.spawn(acquire(semaphore, log, 2));
        auto c = scheduler.spawn(acquire(semaphore, log, 3));
        scheduler.poll(0); assert(log.size == 3);
    }
}
