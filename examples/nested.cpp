// Spawn nested_parent_frame() as one job. Its notifier wakes receive_packet().
#include <coro/frame_slot>
#include "board.hpp"

struct received_packet { std::uint32_t bytes; };

[[coro::noalloc]] coro::task<received_packet> receive_packet() {
    co_await board_scheduler::wait();
    // Board code supplies actual RX metadata here.
    co_return {1};
}

[[coro::noalloc]] coro::task<void> process_packet(received_packet packet) {
    // Process data supplied by the board driver.
    co_await board_scheduler::sleep_for(packet.bytes);
}

[[coro::noalloc]] coro::task<void> packet_loop() {
    // One buffer sized/aligned for the larger child, rather than two frames.
    coro::frame_slot<receive_packet, process_packet> children;
    for (;;) {
        auto packet = co_await children.invoke<receive_packet>();
        co_await children.invoke<process_packet>(packet);
        // Each child is destroyed before the shared slot is reused.
    }
}

coro::frame<packet_loop> nested_parent_frame;
