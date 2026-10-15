// Spawn both worker frames. Board code acknowledges the IRQ before releasing.
#include <coro/frame>
#include <coro_os/mutex.hpp>
#include <coro_os/semaphore.hpp>
#include "board.hpp"

static coro_os::mutex<board_port> shared_mutex;
static coro_os::semaphore<board_port, 8> packets{0};
static volatile std::uint32_t processed = 0;
static volatile std::uint32_t overflow = 0;

extern "C" void SyncPeripheral_IRQHandler() {
    if (!packets.release_from_isr()) overflow = overflow + 1;
}

[[coro::noalloc]] coro::task<void> sync_worker() {
    for (;;) {
        if (!co_await packets.acquire_for(100)) continue;
        auto guard = co_await shared_mutex.lock();
        processed = processed + 1;
        co_await board_scheduler::yield(); // Guard stays owned across suspension.
    }
}
coro::frame<sync_worker> sync_worker_a_frame;
coro::frame<sync_worker> sync_worker_b_frame;
