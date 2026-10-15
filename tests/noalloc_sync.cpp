#include <coro/frame>
#include <coro_os/mutex.hpp>
#include <coro_os/semaphore.hpp>

namespace os = coro_os;
struct sync_port {
    static std::uint32_t enter_critical() noexcept { return 0; }
    static void exit_critical(std::uint32_t) noexcept {}
    static void notify_cpu() noexcept {}
    static bool in_isr() noexcept { return false; }
};
using sync_scheduler = os::scheduler<sync_port>;
using sync_mutex = os::mutex<sync_port>;
using sync_semaphore = os::semaphore<sync_port, 2>;

[[coro::noalloc]] coro::task<void> owner(sync_mutex& mutex) {
    auto guard = co_await mutex.lock();
    co_await sync_scheduler::wait();
}
[[coro::noalloc]] coro::task<int> child(sync_semaphore& semaphore) {
    co_return co_await semaphore.acquire_for(3) ? 7 : -1;
}
[[coro::noalloc]] coro::task<void> consumer(sync_mutex& mutex, sync_semaphore& semaphore, int& result) {
    coro::frame<child> child_frame;
    int value = co_await child_frame(semaphore);
    if (value < 0) { result = value; co_return; }
    auto guard = co_await mutex.lock();
    result += value;
    co_await sync_scheduler::yield();
}
static_assert(sizeof(os::detail::node<sync_port>) == 7 * sizeof(void*) + 8);
static_assert(sizeof(sync_scheduler) == 5 * sizeof(void*));

int main() {
    coro::frame<owner> owner_frame;
    coro::frame<consumer> consumer_frame;
    sync_scheduler scheduler(sync_port{});
    sync_mutex mutex;
    sync_semaphore semaphore;
    for (int i = 0; i != 2; ++i) {
        int result = 0;
        {
            auto holder = scheduler.spawn(owner_frame(mutex));
            auto receiver = scheduler.spawn(consumer_frame(mutex, semaphore, result));
            os::detail::require(scheduler.poll(0) == 3);
            os::detail::require(semaphore.release_from_isr()); scheduler.poll(0);
            os::detail::require(result == 0);
            holder.notifier().signal(); scheduler.poll(0);
            os::detail::require(result == 7);
        }
    }
    {
        int result = 0;
        auto receiver = scheduler.spawn(consumer_frame(mutex, semaphore, result));
        scheduler.poll(0); scheduler.poll(3);
        os::detail::require(result == -1);
    }
    for (bool grant : {false, true}) {
        int result = 0;
        {
            auto receiver = scheduler.spawn(consumer_frame(mutex, semaphore, result));
            scheduler.poll(0);
            if (grant) os::detail::require(semaphore.release_from_isr());
        }
        os::detail::require(result == 0 && scheduler.poll(100) == os::no_deadline);
        int expected = grant ? 7 : -1;
        auto receiver = scheduler.spawn(consumer_frame(mutex, semaphore, result));
        scheduler.poll(0); scheduler.poll(3);
        os::detail::require(result == expected);
    }
    return 0;
}
