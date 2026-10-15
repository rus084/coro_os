#include <coro_os/detail/queues.hpp>
#include <cassert>
#include <cstdint>

using namespace coro_os;
struct queue_port;
using node = detail::node<queue_port>;
using ready_queue = detail::ready_queue<queue_port>;
using timer_queue = detail::timer_queue<queue_port>;
static_assert(sizeof(detail::state) == 1);
static_assert(sizeof(node) == 7 * sizeof(void*) + 8);

int main() {
    node a{}, b{}, c{};
    ready_queue ready;
    ready.push(a); ready.push(b); ready.push(c);
    ready.remove(b);
    assert(ready.pop() == &a);
    assert(ready.pop() == &c);
    assert(ready.pop() == nullptr);
    ready.push(a); ready.push(b); ready.remove(a); ready.remove(b);
    assert(ready.empty());

    timer_queue timers;
    timers.insert(a, 3); timers.insert(b, 3); timers.insert(c, 10);
    tick_t elapsed = 2;
    assert(timers.advance_one(elapsed) == nullptr);
    assert(elapsed == 0 && timers.next_delay() == 1);
    elapsed = 1;
    assert(timers.advance_one(elapsed) == &a);
    assert(timers.advance_one(elapsed) == &b);
    assert(timers.advance_one(elapsed) == nullptr);
    assert(timers.next_delay() == 7);
    timers.remove(c);
    assert(timers.next_delay() == no_deadline);

    // Reusing the link across queue transitions must not affect other nodes.
    timers.insert(a, 2); timers.insert(b, 5);
    elapsed = 2;
    assert(timers.advance_one(elapsed) == &a);
    ready.push(a); ready.push(c);
    timers.remove(a); // In ready only: must preserve its link to c.
    ready.remove(b);  // In timer only: must preserve timer membership.
    assert(timers.next_delay() == 3);
    assert(ready.pop() == &a && ready.pop() == &c);
    timers.remove(b);
    ready.push(a);
    assert(ready.pop() == &a);
    timers.insert(a, 1);
    elapsed = 1;
    assert(timers.advance_one(elapsed) == &a);

    for (int removed = 0; removed < 3; ++removed) {
        timers.insert(a, 3); timers.insert(b, 7); timers.insert(c, 10);
        node* nodes[] = {&a, &b, &c};
        const tick_t deadlines[] = {3, 7, 10};
        timers.remove(*nodes[removed]);
        tick_t now = 0;
        for (int i = 0; i < 3; ++i) {
            if (i == removed) continue;
            elapsed = timers.next_delay(); now += elapsed;
            assert(timers.advance_one(elapsed) == nodes[i]);
            assert(now == deadlines[i]);
        }
        assert(timers.next_delay() == no_deadline);
    }
    timers.insert(a, UINT32_MAX - 1);
    timers.insert(b, UINT32_MAX - 1);
    elapsed = UINT32_MAX;
    assert(timers.advance_one(elapsed) == &a);
    assert(timers.advance_one(elapsed) == &b);
    assert(timers.advance_one(elapsed) == nullptr);
}
