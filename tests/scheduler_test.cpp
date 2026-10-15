#include <coro_os/scheduler.hpp>
#include "support/test_port.hpp"
#include "support/test_task.hpp"
#include <array>
#include <type_traits>

using namespace coro_os;
struct trace {
    std::array<int, 64> data{};
    unsigned size = 0;
    void add(int v) { assert(size < data.size()); data[size++] = v; }
};
test_task yielding(test_port& p, trace& log, int id) {
    for (int i = 0; i != 20; ++i) {
        assert(p.depth == 0); log.add(id); co_await test_scheduler::yield();
    }
}
test_task sleeper(trace& log, int id, tick_t delay) {
    co_await test_scheduler::sleep_for(delay); log.add(id);
}
test_task once(trace& log) { log.add(9); co_return; }

int main() {
    test_port p;
    test_scheduler s(p);
    static_assert(!std::is_move_constructible_v<job<test_task, test_port&>>);
    static_assert(!std::is_copy_constructible_v<job<test_task, test_port&>>);
    trace log;
    {
        auto a = s.spawn(yielding(p, log, 1));
        auto b = s.spawn(yielding(p, log, 2));
        assert(log.size == 0);
        assert(s.poll(0) == no_deadline && log.size == 40);
        for (unsigned i = 0; i != log.size; ++i)
            assert(log.data[i] == (i % 2 == 0 ? 1 : 2));
    }
    assert(s.poll(0) == no_deadline);
    log = {};
    {
        auto a = s.spawn(sleeper(log, 1, 5));
        auto b = s.spawn(sleeper(log, 2, 5));
        assert(s.poll(0) == 5 && log.size == 0);
        assert(s.poll(4) == 1 && log.size == 0);
        assert(s.poll(1) == no_deadline);
        assert(log.size == 2 && log.data[0] == 1 && log.data[1] == 2);
        s.poll(20); assert(log.size == 2);
    }
    log = {};
    {
        auto a = s.spawn(sleeper(log, 1, 0));
        auto b = s.spawn(once(log));
        assert(s.poll(0) == no_deadline);
        assert(log.size == 2 && log.data[0] == 9 && log.data[1] == 1);
    }
    log = {};
    {
        auto a = s.spawn(sleeper(log, 1, UINT32_MAX - 1));
        assert(s.poll(0) == UINT32_MAX - 1);
        s.poll(UINT32_MAX); assert(log.size == 1);
    }
    log = {};
    {
        auto a = s.spawn(sleeper(log, 1, 5));
        // Elapsed before this coroutine started must not count against its new sleep.
        assert(s.poll(100) == 5);
    }
    assert(s.poll(100) == no_deadline && log.size == 0);
}
